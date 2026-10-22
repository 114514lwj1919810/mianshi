#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
集成控制模块 - 拼音指令匹配 + 分模式空闲超时版

主要功能：
1. 使用拼音音节序列匹配 Vosk 结果，使“向佐/左传/前景”等同音识别结果仍能触发正确指令。
2. 指令关键词的拼音在程序启动时预计算；运行时只转换当前 ASR 文本，保证实时性。
3. 只允许“整句命令”或“位于句尾且具有控制语气”的命令触发，减少普通聊天误触发。
4. 高风险近音结果只允许 final 整句匹配，例如 Vosk 把“左转”识别成“坐船”。
5. 语音模式：连续 60 秒没有识别到语音则停止并休眠。
6. 头部模式：连续 60 秒没有检测到人脸/头部姿态则停止并休眠。
"""

import os
import re
import time
import json
import threading
from collections import deque
from dataclasses import dataclass
from functools import lru_cache
from typing import List, Tuple, Optional, Dict

import cv2
import numpy as np
import pyaudio
from vosk import Model, KaldiRecognizer

try:
    from pypinyin import lazy_pinyin, Style
    PYPINYIN_AVAILABLE = True
except Exception:
    lazy_pinyin = None
    Style = None
    PYPINYIN_AVAILABLE = False

from config import VOICE_MODEL_PATH, YOLO_MODEL_PATH, CAMERA_ID, AUDIO_DIR
from utils.message_queue import msg_queue, MessageType
from utils.logger import get_logger

logger = get_logger(__name__)

# --- 环境依赖检查 ---
try:
    import pygame
    PYGAME_AVAILABLE = True
except Exception:
    PYGAME_AVAILABLE = False

# --- 全局状态 ---
SYSTEM_ACTIVE = False
system_active_lock = threading.Lock()

MODE_VOICE = "voice"
MODE_HEAD = "head"
current_mode = MODE_VOICE
mode_lock = threading.Lock()

# --- 实时语音显示 ---
RECENT_SPEECH = ""
SPEECH_LOCK = threading.Lock()
last_speech_update_time = 0
SPEECH_UPDATE_COOLDOWN = 0.2

# YOLO 配置
MODEL_INPUT_SIZE = 128
# 必须与 ONNX 模型元数据/训练 data.yaml 的 names 顺序一致。
CLASSES = ["front", "right", "up", "left", "down"]
MODEL_SCORE_THRESHOLD = 0.45
MODEL_NMS_THRESHOLD = 0.50
LETTERBOX_FOR_SQUARE = True

# --- 指令配置 ---
# 所有关键词最终都转换为拼音音节进行匹配，不再直接比较汉字。
# 为降低误触发，单音节“左/右/停”不再作为命令；请说“左转/右转/停止”。
command_config = {
    "前进": {
        "code": "F",
        "voice": "前进",
        "keywords": ["前进", "向前", "往前", "向前走", "往前走", "直走", "前走"],
        "exact_keywords": [],
        # Vosk 有时把“前进”识别为“前景”(qian jing)，仅允许 final 整句补偿。
        "final_only_pinyin_aliases": ["qian jing"],
        "min_confidence": 0.82,
    },
    "后退": {
        "code": "B",
        "voice": "后退",
        "keywords": ["后退", "向后", "往后", "倒车", "向后退", "往后退"],
        "exact_keywords": [],
        "final_only_pinyin_aliases": [],
        "min_confidence": 0.82,
    },
    "向左": {
        "code": "L",
        "voice": "左转",
        "keywords": ["向左", "左转", "往左", "向左转", "往左转", "转左"],
        "exact_keywords": [],
        # “坐船”(zuo chuan) 与“左转”(zuo zhuan)并非同音，无法靠普通拼音等值匹配。
        # 这里只允许它作为 final 的完整两音节结果触发，避免它出现在长句里时误触发。
        "final_only_pinyin_aliases": ["zuo chuan"],
        "min_confidence": 0.82,
    },
    "向右": {
        "code": "R",
        "voice": "右转",
        "keywords": ["向右", "右转", "往右", "向右转", "往右转", "转右"],
        "exact_keywords": [],
        "final_only_pinyin_aliases": [],
        "min_confidence": 0.82,
    },
    "停止": {
        "code": "S",
        "voice": "停止",
        "keywords": ["停止", "停下", "停下来", "别动", "站住"],
        "exact_keywords": [],
        "final_only_pinyin_aliases": [],
        "min_confidence": 0.80,
    },
    "加速": {
        "code": "A",
        "voice": "GEAR_UP",
        "keywords": ["加速", "快点", "加快", "快一点", "提速", "快些"],
        "exact_keywords": [],
        "final_only_pinyin_aliases": [],
        "min_confidence": 0.82,
    },
    "减速": {
        "code": "D",
        "voice": "GEAR_DOWN",
        "keywords": ["减速", "慢点", "减慢", "慢一点", "慢些"],
        "exact_keywords": [],
        "final_only_pinyin_aliases": [],
        "min_confidence": 0.82,
    },
    "开始": {
        "code": "START",
        "voice": "启动",
        "keywords": ["开始", "启动", "开启", "唤醒", "启动系统", "开始工作", "启动机器人"],
        "exact_keywords": [],
        "final_only_pinyin_aliases": [],
        "min_confidence": 0.80,
    },
    "关机": {
        "code": "X",
        "voice": "已关闭",
        "keywords": ["关机", "关闭", "关掉", "关闭系统", "休眠", "结束"],
        "exact_keywords": [],
        "final_only_pinyin_aliases": [],
        "min_confidence": 0.80,
    },
    "语音模式": {
        "code": "MODE_VOICE",
        "voice": "语音模式",
        "keywords": ["语音模式", "语音控制", "切换到语音", "使用语音控制", "开启语音控制"],
        "exact_keywords": ["语音"],
        "final_only_pinyin_aliases": [],
        "min_confidence": 0.80,
    },
    "头部模式": {
        "code": "MODE_HEAD",
        "voice": "头部模式",
        "keywords": ["头部模式", "头部控制", "切换到头部", "使用头部控制", "开启头部控制"],
        "exact_keywords": ["头部"],
        "final_only_pinyin_aliases": [],
        "min_confidence": 0.80,
    },
}

PUNCT_RE = re.compile(r"[\s,，.。!！?？;；:：、\-_/\\|\[\]()（）<>《》]+")
PINYIN_TOKEN_RE = re.compile(r"^[a-zv]+$")

# 允许放在命令后面的语气词；匹配前先从句尾移除。
TRAILING_PINYIN_SUFFIXES = (
    ("yi", "xia"),
    ("ba",),
    ("a",),
    ("ya",),
    ("la",),
    ("ne",),
    ("le",),
)

# 两音节命令出现在长句末尾时，前文必须具有明显的控制语气。
# 三音节及以上的明确命令可直接在句尾触发。
CONTROL_CUE_SEQUENCES = (
    ("qing",),
    ("qing", "ni"),
    ("gei", "wo"),
    ("bang", "wo"),
    ("ma", "shang"),
    ("gan", "jin"),
    ("xian",),
    ("xian", "zai"),
    ("ran", "hou"),
    ("jie", "zhe"),
    ("ji", "xu"),
    ("kai", "shi"),
    ("wo", "yao"),
    ("wo", "xiang"),
    ("xiao", "che"),
    ("ji", "qi", "ren"),
)


def _normalize_pinyin_token(token: str) -> str:
    return token.lower().replace("u:", "v").replace("ü", "v").strip()


@lru_cache(maxsize=512)
def text_to_pinyin_tokens(text: str) -> Tuple[str, ...]:
    """把 ASR 文本转换成无声调拼音音节；结果缓存，降低 partial 重复转换开销。"""
    if not text or not PYPINYIN_AVAILABLE:
        return tuple()

    clean_text = PUNCT_RE.sub("", text.strip().lower())
    if not clean_text:
        return tuple()

    raw_tokens = lazy_pinyin(
        clean_text,
        style=Style.NORMAL,
        neutral_tone_with_five=False,
        errors=lambda chars: [],
    )
    tokens = []
    for token in raw_tokens:
        normalized = _normalize_pinyin_token(token)
        if normalized and PINYIN_TOKEN_RE.match(normalized):
            tokens.append(normalized)
    return tuple(tokens)


def parse_pinyin_alias(alias: str) -> Tuple[str, ...]:
    return tuple(
        token
        for token in (_normalize_pinyin_token(x) for x in alias.split())
        if token and PINYIN_TOKEN_RE.match(token)
    )


@dataclass(frozen=True)
class PinyinAlias:
    name: str
    code: str
    keyword: str
    tokens: Tuple[str, ...]
    min_confidence: float
    exact_only: bool = False
    final_only: bool = False


@dataclass
class CommandHit:
    name: str
    code: str
    confidence: float
    start: int
    end: int
    keyword: str
    pinyin: str
    final_only: bool = False

    @property
    def span_key(self) -> str:
        return f"{self.name}:{self.pinyin}:{self.start}:{self.end}:{int(self.final_only)}"


def build_pinyin_alias_index() -> Tuple[PinyinAlias, ...]:
    """启动时一次性把所有中文关键词转换成拼音。"""
    aliases = []
    seen = set()

    for cmd_name, cfg in command_config.items():
        code = cfg["code"]
        min_conf = float(cfg.get("min_confidence", 0.80))

        for keyword in cfg.get("keywords", []):
            tokens = text_to_pinyin_tokens(keyword)
            key = (cmd_name, tokens, False, False)
            if tokens and key not in seen:
                aliases.append(PinyinAlias(cmd_name, code, keyword, tokens, min_conf))
                seen.add(key)

        for keyword in cfg.get("exact_keywords", []):
            tokens = text_to_pinyin_tokens(keyword)
            key = (cmd_name, tokens, True, False)
            if tokens and key not in seen:
                aliases.append(PinyinAlias(
                    cmd_name, code, keyword, tokens, min_conf,
                    exact_only=True,
                ))
                seen.add(key)

        for alias_text in cfg.get("final_only_pinyin_aliases", []):
            tokens = parse_pinyin_alias(alias_text)
            key = (cmd_name, tokens, True, True)
            if tokens and key not in seen:
                aliases.append(PinyinAlias(
                    cmd_name, code, alias_text, tokens, min_conf,
                    exact_only=True,
                    final_only=True,
                ))
                seen.add(key)

    # 较长关键词优先，例如“向左转”优先于“向左”。
    aliases.sort(key=lambda item: len(item.tokens), reverse=True)
    return tuple(aliases)


PINYIN_ALIAS_INDEX = build_pinyin_alias_index() if PYPINYIN_AVAILABLE else tuple()


def strip_trailing_pinyin_suffixes(tokens: Tuple[str, ...]) -> Tuple[str, ...]:
    trimmed = tuple(tokens)
    changed = True
    while trimmed and changed:
        changed = False
        for suffix in TRAILING_PINYIN_SUFFIXES:
            if len(trimmed) >= len(suffix) and trimmed[-len(suffix):] == suffix:
                trimmed = trimmed[:-len(suffix)]
                changed = True
                break
    return trimmed


def has_control_cue(prefix_tokens: Tuple[str, ...]) -> bool:
    """只检查命令前最近 6 个音节，避免很早以前的提示词影响当前语句。"""
    window = prefix_tokens[-6:]
    for cue in CONTROL_CUE_SEQUENCES:
        cue_len = len(cue)
        if cue_len <= len(window):
            for i in range(len(window) - cue_len + 1):
                if window[i:i + cue_len] == cue:
                    return True
    return False


def extract_command_hits(text: str, result_type: str = "partial") -> List[CommandHit]:
    """
    使用拼音音节匹配命令。

    安全规则：
    1. 完整一句就是命令：允许立即触发。
    2. 命令位于句尾：三音节以上直接允许；两音节命令需要控制提示词。
    3. 高风险近音别名只允许 final 且整句完全一致。
    4. 不在句尾的旧命令不触发，避免 partial 累积文本反复执行。
    """
    tokens = text_to_pinyin_tokens(text)
    if not tokens:
        return []

    trimmed_tokens = strip_trailing_pinyin_suffixes(tokens)
    hits: List[CommandHit] = []

    for alias in PINYIN_ALIAS_INDEX:
        alias_len = len(alias.tokens)
        if alias_len == 0:
            continue
        if alias.final_only and result_type != "final":
            continue

        # 高风险近音项必须与原始整句完全一致，不允许“坐船吧”或长句中的“坐船”触发。
        if alias.final_only:
            if tokens != alias.tokens:
                continue
            confidence = 0.90
            hits.append(CommandHit(
                alias.name, alias.code, confidence,
                0, alias_len, alias.keyword,
                " ".join(alias.tokens),
                final_only=True,
            ))
            continue

        # 完整一句等于命令，可靠性最高。
        if tokens == alias.tokens:
            confidence = 0.99
            hits.append(CommandHit(
                alias.name, alias.code, confidence,
                0, alias_len, alias.keyword,
                " ".join(alias.tokens),
            ))
            continue

        if alias.exact_only:
            continue

        # 命令必须位于去掉语气词后的句尾。
        if len(trimmed_tokens) < alias_len or trimmed_tokens[-alias_len:] != alias.tokens:
            continue

        start = len(trimmed_tokens) - alias_len
        prefix = trimmed_tokens[:start]

        if not prefix:
            confidence = 0.97
        elif alias_len >= 3:
            confidence = 0.91
        elif has_control_cue(prefix):
            confidence = 0.88
        else:
            # 两音节命令出现在普通长句末尾但没有控制提示词，不执行。
            continue

        if confidence >= alias.min_confidence:
            hits.append(CommandHit(
                alias.name, alias.code, confidence,
                start, start + alias_len, alias.keyword,
                " ".join(alias.tokens),
            ))

    return hits


def smart_match(
    text: str,
    result_type: str = "partial",
) -> Tuple[Optional[str], Optional[str], float, Optional[CommandHit]]:
    """返回句尾最后出现、置信度最高、拼音关键词最长的有效指令。"""
    hits = extract_command_hits(text, result_type=result_type)
    if not hits:
        return None, None, 0.0, None

    best = max(
        hits,
        key=lambda h: (h.end, h.confidence, len(h.pinyin.split())),
    )
    return best.name, best.code, best.confidence, best


# --- 语音显示功能 ---
def update_recent_speech(text: str, current_time: float) -> bool:
    """更新最近识别的语音文本。"""
    global RECENT_SPEECH, last_speech_update_time

    if not text or len(text.strip()) < 2:
        return False

    if current_time - last_speech_update_time < SPEECH_UPDATE_COOLDOWN:
        return False

    with SPEECH_LOCK:
        if text != RECENT_SPEECH:
            RECENT_SPEECH = text
            last_speech_update_time = current_time
            return True

    return False


def clear_recent_speech():
    """清空显示缓存，避免触发后继续打印旧 partial。"""
    global RECENT_SPEECH
    with SPEECH_LOCK:
        RECENT_SPEECH = ""


def get_recent_speech() -> str:
    """获取最近识别的语音文本。"""
    with SPEECH_LOCK:
        return RECENT_SPEECH


# --- 模式切换 ---
def switch_mode(new_mode, voice_feedback=None):
    """模式切换。"""
    global current_mode
    with mode_lock:
        if new_mode == current_mode:
            return

        msg_queue.put(MessageType.CONTROL_COMMAND, "S")
        current_mode = new_mode

        mode_name = "语音控制" if new_mode == MODE_VOICE else "头部控制"
        logger.info(f"=== 已切换到 {mode_name} 模式 ===")

        if voice_feedback:
            voice_feedback.play_switch_mode(new_mode)


# --- 语音反馈类 ---
class VoiceFeedback:
    def __init__(self, audio_dir):
        self.audio_dir = audio_dir
        self.enabled = False
        self.virtual_gear = 1
        self.is_playing = False
        self.last_play_time = 0
        self.play_cooldown = 0.3

        logger.info(f"初始化语音反馈，音频目录: {audio_dir}")

        if not PYGAME_AVAILABLE:
            logger.warning("pygame 未安装，语音反馈不可用")
            return

        try:
            pygame.mixer.init()
            self.enabled = True
            logger.info("语音反馈系统初始化成功")
        except Exception as e:
            logger.warning(f"语音反馈系统初始化失败: {e}")

    def play_sound(self, sound_name, force=False):
        """播放声音。"""
        if not self.enabled or not sound_name:
            return False

        current_time = time.time()

        if not force and (current_time - self.last_play_time) < self.play_cooldown:
            return False

        try:
            if self.is_playing:
                pygame.mixer.music.stop()
                time.sleep(0.05)

            file_path = os.path.join(self.audio_dir, f"{sound_name}.mp3")
            if not os.path.exists(file_path):
                logger.warning(f"语音文件缺失: {file_path}")
                return False

            pygame.mixer.music.load(file_path)
            pygame.mixer.music.play()
            self.is_playing = True
            self.last_play_time = current_time

            logger.info(f"播放语音: {sound_name}")
            return True
        except Exception as e:
            logger.error(f"播放语音失败: {e}")
            return False

    def play_startup(self):
        return self.play_sound("启动")

    def play_not_awake(self):
        return self.play_sound("未唤醒")

    def play_switch_mode(self, mode):
        if mode == MODE_VOICE:
            return self.play_sound("语音模式")
        if mode == MODE_HEAD:
            return self.play_sound("头部模式")
        return False

    def play_command_voice(self, command_name, command_code):
        if not command_name:
            return False

        if command_name == "关机":
            return self.play_sound("已关闭")

        if command_name == "语音模式":
            return self.play_switch_mode(MODE_VOICE)

        if command_name == "头部模式":
            return self.play_switch_mode(MODE_HEAD)

        if command_name == "开始":
            return self.play_startup()

        if command_name == "加速":
            if self.virtual_gear < 3:
                self.virtual_gear += 1
                gear_sounds = ["", "一档", "二档", "三档"]
                return self.play_sound(gear_sounds[self.virtual_gear])
            return self.play_sound("已是最高速度")

        if command_name == "减速":
            if self.virtual_gear > 1:
                self.virtual_gear -= 1
                gear_sounds = ["", "一档", "二档", "三档"]
                return self.play_sound(gear_sounds[self.virtual_gear])
            return self.play_sound("已是最低速度")

        voice_file = command_config.get(command_name, {}).get("voice")
        if voice_file:
            return self.play_sound(voice_file)

        return False

    def play_pose_voice(self, pose_code):
        mapping = {"F": "前进", "B": "后退", "L": "左转", "R": "右转", "S": "停止"}
        if pose_code in mapping:
            return self.play_sound(mapping[pose_code])
        return False


# --- 头部姿态检测相关类 ---
@dataclass
class Detection:
    class_id: int
    class_name: str
    confidence: float
    box: Tuple[int, int, int, int]


def sigmoid(x: np.ndarray) -> np.ndarray:
    return 1.0 / (1.0 + np.exp(-x))


def format_to_square(img_bgr: np.ndarray) -> np.ndarray:
    h, w = img_bgr.shape[:2]
    m = max(h, w)
    out = np.zeros((m, m, 3), dtype=img_bgr.dtype)
    out[0:h, 0:w] = img_bgr
    return out


class HeadPoseYoloOpenCV:
    def __init__(self, onnx_path: str, front_bias_threshold: float = 0.1):
        self.net = cv2.dnn.readNetFromONNX(onnx_path)
        self.net.setPreferableBackend(cv2.dnn.DNN_BACKEND_OPENCV)
        self.net.setPreferableTarget(cv2.dnn.DNN_TARGET_CPU)
        try:
            cv2.setNumThreads(1)
        except Exception:
            pass
        self.front_bias_threshold = front_bias_threshold

    def infer(self, frame_bgr: np.ndarray) -> List[Detection]:
        if frame_bgr is None or frame_bgr.size == 0:
            return []

        model_input = frame_bgr
        if LETTERBOX_FOR_SQUARE:
            model_input = format_to_square(model_input)

        blob = cv2.dnn.blobFromImage(
            model_input,
            scalefactor=1.0 / 255.0,
            size=(MODEL_INPUT_SIZE, MODEL_INPUT_SIZE),
            mean=(0, 0, 0),
            swapRB=True,
            crop=False,
        )
        self.net.setInput(blob)
        out_names = self.net.getUnconnectedOutLayersNames()
        outs = self.net.forward(out_names)
        out0 = outs[0]

        if out0.ndim != 3 or out0.shape[0] != 1:
            raise RuntimeError(f"Unexpected output shape: {out0.shape}")

        rows = out0.shape[1]
        dims = out0.shape[2]
        if dims > rows:
            out2d = out0[0].T.astype(np.float32, copy=False)
            rows, dims = out2d.shape
        else:
            out2d = out0[0].astype(np.float32, copy=False)

        x_factor = model_input.shape[1] / float(MODEL_INPUT_SIZE)
        y_factor = model_input.shape[0] / float(MODEL_INPUT_SIZE)
        boxes, scores, class_ids = [], [], []

        for i in range(rows):
            data = out2d[i]
            # 当前导出的 ONNX 检测头已经输出 0~1 类别概率，不再重复 sigmoid。
            cls_scores = data[4:4 + len(CLASSES)]
            cid = int(np.argmax(cls_scores))
            max_score = float(cls_scores[cid])

            if max_score > MODEL_SCORE_THRESHOLD:
                if CLASSES[cid] in ("left", "right"):
                    front_idx = CLASSES.index("front")
                    front_score = float(cls_scores[front_idx])
                    if front_score > 0 and (max_score - front_score) < self.front_bias_threshold:
                        cid = front_idx
                        max_score = front_score

                x, y, w, h = map(float, data[0:4])
                left = int((x - 0.5 * w) * x_factor)
                top = int((y - 0.5 * h) * y_factor)
                width = int(w * x_factor)
                height = int(h * y_factor)
                boxes.append([left, top, width, height])
                class_ids.append(cid)
                scores.append(max_score)

        idxs = cv2.dnn.NMSBoxes(
            bboxes=boxes,
            scores=scores,
            score_threshold=MODEL_SCORE_THRESHOLD,
            nms_threshold=MODEL_NMS_THRESHOLD,
        )

        dets = []
        if len(idxs) > 0:
            for idx in np.array(idxs).flatten().tolist():
                x, y, w, h = boxes[idx]
                cid = class_ids[idx]
                dets.append(
                    Detection(
                        class_id=cid,
                        class_name=CLASSES[cid] if 0 <= cid < len(CLASSES) else "unknown",
                        confidence=float(scores[idx]),
                        box=(x, y, w, h),
                    )
                )
        return dets


def pick_best_detection(dets: List[Detection]) -> Optional[Detection]:
    return max(dets, key=lambda d: d.confidence) if dets else None


def pose_to_char(pose: str) -> str:
    if pose == "up":
        return "S"
    if pose == "down":
        return "B"
    if pose == "left":
        return "L"
    if pose == "right":
        return "R"
    if pose == "front":
        return "F"
    return "S"


# --- 主控制类 ---
class IntegratedControl:
    def __init__(self):
        self.running = False
        self.voice_feedback = VoiceFeedback(AUDIO_DIR)
        self.head_cap = None
        self.head_infer = None
        self.voice_thread = None
        self.head_thread = None
        self.timeout_thread = None

        # 分模式空闲计时：语音模式看语音活动，头部模式看人脸/姿态活动。
        now = time.time()
        self.activity_lock = threading.Lock()
        self.last_voice_activity_time = now
        self.last_face_activity_time = now

        # 语音识别状态
        self.last_processed_time = 0.0
        self.last_trigger_time = 0.0
        self.last_special_command = {"command": None, "time": 0.0}

        # 控制参数
        self.MIN_PROCESS_INTERVAL = 0.12
        self.GLOBAL_COMMAND_COOLDOWN = 0.45
        self.SAME_COMMAND_COOLDOWN = 1.0
        self.SPECIAL_COMMAND_COOLDOWN = 1.0
        self.MODE_IDLE_TIMEOUT = 60
        self.ECHO_SUPPRESS_SECONDS = 0.75
        self.NO_COMMAND_RESET_INTERVAL = 6.0

        # 替代旧 command_history[-3:] 的按指令冷却记录
        self.last_command_time_by_name: Dict[str, float] = {}
        self.recent_hit_signatures = deque(maxlen=30)
        self.ignore_asr_until = 0.0
        self.last_partial_change_time = time.time()
        self.last_partial_text = ""

        logger.info("集成控制模块初始化完成")

    def reset_mode_activity(self, mode: str, when: Optional[float] = None):
        """切换模式或重新唤醒时，从当前时刻重新开始计算该模式的 60 秒空闲时间。"""
        timestamp = when if when is not None else time.time()
        with self.activity_lock:
            if mode == MODE_HEAD:
                self.last_face_activity_time = timestamp
            else:
                self.last_voice_activity_time = timestamp

    def mark_voice_activity(self, when: Optional[float] = None):
        timestamp = when if when is not None else time.time()
        with self.activity_lock:
            self.last_voice_activity_time = timestamp

    def mark_face_activity(self, when: Optional[float] = None):
        timestamp = when if when is not None else time.time()
        with self.activity_lock:
            self.last_face_activity_time = timestamp

    def deactivate_for_timeout(self, reason: str) -> bool:
        """空闲超时后先发送停止，再进入未唤醒状态。"""
        global SYSTEM_ACTIVE
        with system_active_lock:
            if not SYSTEM_ACTIVE:
                return False
            SYSTEM_ACTIVE = False

        msg_queue.put(MessageType.CONTROL_COMMAND, "S")
        logger.info(f"{reason}，已发送停止指令并进入未唤醒状态")
        self.voice_feedback.play_not_awake()
        self.ignore_asr_until = time.time() + self.ECHO_SUPPRESS_SECONDS
        return True

    def activity_watchdog_thread(self):
        """根据当前模式检查对应的活动来源，互不干扰。"""
        logger.info("分模式空闲监控线程已启动")
        while self.running:
            now = time.time()

            with system_active_lock:
                active = SYSTEM_ACTIVE
            with mode_lock:
                mode = current_mode

            if active:
                with self.activity_lock:
                    if mode == MODE_HEAD:
                        last_activity = self.last_face_activity_time
                    else:
                        last_activity = self.last_voice_activity_time

                idle_seconds = now - last_activity
                if idle_seconds >= self.MODE_IDLE_TIMEOUT:
                    if mode == MODE_HEAD:
                        self.deactivate_for_timeout(
                            f"头部模式连续 {self.MODE_IDLE_TIMEOUT} 秒未检测到人脸/头部姿态"
                        )
                    else:
                        self.deactivate_for_timeout(
                            f"语音模式连续 {self.MODE_IDLE_TIMEOUT} 秒未检测到语音"
                        )

            time.sleep(0.5)

        logger.info("分模式空闲监控线程已退出")

    def voice_recognition_thread(self):
        """流式语音识别：使用拼音音节进行实时命令匹配。"""
        if not PYPINYIN_AVAILABLE:
            logger.error("缺少 pypinyin，语音控制无法启动。请执行: pip install pypinyin")
            return

        if not os.path.exists(VOICE_MODEL_PATH):
            logger.error(f"语音模型路径 '{VOICE_MODEL_PATH}' 不存在")
            return

        try:
            model = Model(VOICE_MODEL_PATH)
            logger.info("语音模型加载成功")
        except Exception as e:
            logger.error(f"模型加载失败: {e}")
            return

        recognizer = KaldiRecognizer(model, 16000)
        # 有词表时可减少误识别；如果你的模型支持 grammar，也可以打开下面这一类限制词表。
        # recognizer = KaldiRecognizer(model, 16000, json.dumps([
        #     "开始", "启动", "前进", "向前", "后退", "向后", "左转", "右转",
        #     "停止", "加速", "减速", "关机", "语音模式", "头部模式", "[unk]"
        # ], ensure_ascii=False))

        p = pyaudio.PyAudio()
        stream = p.open(
            format=pyaudio.paInt16,
            channels=1,
            rate=16000,
            input=True,
            frames_per_buffer=1024,
        )
        stream.start_stream()

        last_no_command_reset = time.time()
        logger.info(">>> 语音线程启动 [拼音指令匹配版]")

        try:
            while self.running:
                data = stream.read(1024, exception_on_overflow=False)
                if len(data) == 0:
                    continue

                current_time = time.time()

                # 播放提示音后的短暂窗口内忽略 ASR，避免机器人自己的“右转/前进”反馈被再次识别。
                if current_time < self.ignore_asr_until:
                    continue

                is_final = recognizer.AcceptWaveform(data)

                if is_final:
                    result_data = json.loads(recognizer.Result())
                    text = result_data.get("text", "").strip()
                    result_type = "final"
                else:
                    partial_data = json.loads(recognizer.PartialResult())
                    text = partial_data.get("partial", "").strip()
                    result_type = "partial"

                if text:
                    update_recent_speech(text, current_time)

                    # 语音模式的 60 秒计时按“检测到任何语音文本”刷新，
                    # 不要求必须识别成控制命令；头部模式不使用这个计时。
                    with system_active_lock:
                        active = SYSTEM_ACTIVE
                    with mode_lock:
                        mode = current_mode
                    if active and mode == MODE_VOICE:
                        self.mark_voice_activity(current_time)

                    if text != self.last_partial_text:
                        self.last_partial_text = text
                        self.last_partial_change_time = current_time

                    if self.process_smart_detection(text, current_time, result_type):
                        recognizer.Reset()
                        clear_recent_speech()
                        self.last_partial_text = ""
                        self.last_partial_change_time = current_time
                        last_no_command_reset = current_time
                        continue

                # final 结果已结束一句话，处理完后重置，避免旧句子进入下一段 partial。
                if is_final:
                    recognizer.Reset()
                    self.last_partial_text = ""
                    clear_recent_speech()
                    last_no_command_reset = current_time

                # 长时间无有效指令时重置 recognizer，防止 partial 长句无限累积。
                if current_time - last_no_command_reset > self.NO_COMMAND_RESET_INTERVAL:
                    recognizer.Reset()
                    self.last_partial_text = ""
                    clear_recent_speech()
                    last_no_command_reset = current_time

        except Exception as e:
            logger.error(f"语音识别错误: {e}")
        finally:
            try:
                stream.stop_stream()
                stream.close()
                p.terminate()
            except Exception:
                pass
            logger.info("语音线程已退出")

    def process_smart_detection(self, text: str, current_time: float, result_type: str = "partial") -> bool:
        """智能指令检测。"""
        global SYSTEM_ACTIVE, current_mode

        if not text or len(text.strip()) < 2:
            return False

        # 最小处理间隔，避免每个音频帧都做重复逻辑。
        if current_time - self.last_processed_time < self.MIN_PROCESS_INTERVAL:
            return False

        cmd_name, cmd_code, confidence, hit = smart_match(text, result_type=result_type)
        if not cmd_name or not hit:
            return False

        # partial 结果更不稳定，阈值略高；final 结果可略低。
        min_confidence = 0.74 if result_type == "partial" else 0.68
        if confidence < min_confidence:
            return False

        # 全局冷却：避免一句话中 partial 连续触发多个重复命令。
        if current_time - self.last_trigger_time < self.GLOBAL_COMMAND_COOLDOWN:
            return False

        # 同一指令冷却：替代旧的 command_history[-3:]。
        # 旧逻辑会导致“右转”触发一次后，后面再说“右转”被长期误判为重复。
        last_same_time = self.last_command_time_by_name.get(cmd_name, 0.0)
        if current_time - last_same_time < self.SAME_COMMAND_COOLDOWN:
            return False

        # 相同文本片段短时间重复，通常是 Vosk partial 重复或语音反馈回声。
        signature = f"{' '.join(text_to_pinyin_tokens(text))}::{hit.span_key}"
        for old_sig, old_time in list(self.recent_hit_signatures):
            if old_sig == signature and current_time - old_time < 2.0:
                return False

        logger.info(
            f"检测到指令: '{cmd_name}' 置信度: {confidence:.2f} "
            f"类型: {result_type} 关键词: '{hit.keyword}' 拼音: '{hit.pinyin}' 文本: '{text}'"
        )

        self.last_processed_time = current_time
        self.last_trigger_time = current_time
        self.last_command_time_by_name[cmd_name] = current_time
        self.recent_hit_signatures.append((signature, current_time))

        # 处理唤醒逻辑
        if cmd_name == "开始":
            self.voice_feedback.play_startup()
            self.ignore_asr_until = time.time() + self.ECHO_SUPPRESS_SECONDS
            with system_active_lock:
                SYSTEM_ACTIVE = True
            with mode_lock:
                active_mode = current_mode
            self.reset_mode_activity(active_mode, current_time)
            logger.info("系统已唤醒，当前模式空闲计时已重新开始")
            return True

        # 特殊命令冷却
        is_special_command = cmd_name in ["关机", "语音模式", "头部模式"]
        if is_special_command:
            if (
                cmd_name == self.last_special_command["command"]
                and current_time - self.last_special_command["time"] < self.SPECIAL_COMMAND_COOLDOWN
            ):
                return False
            self.last_special_command = {"command": cmd_name, "time": current_time}

        # 功能指令
        if cmd_code == "MODE_VOICE":
            switch_mode(MODE_VOICE, self.voice_feedback)
            self.reset_mode_activity(MODE_VOICE, current_time)
            self.ignore_asr_until = time.time() + self.ECHO_SUPPRESS_SECONDS
            return True

        if cmd_code == "MODE_HEAD":
            switch_mode(MODE_HEAD, self.voice_feedback)
            self.reset_mode_activity(MODE_HEAD, current_time)
            self.ignore_asr_until = time.time() + self.ECHO_SUPPRESS_SECONDS
            return True

        if cmd_name == "关机":
            msg_queue.put(MessageType.CONTROL_COMMAND, "X")
            self.voice_feedback.play_command_voice("关机", "X")
            self.ignore_asr_until = time.time() + self.ECHO_SUPPRESS_SECONDS
            with system_active_lock:
                SYSTEM_ACTIVE = False
            logger.info("系统关闭")
            return True

        # 普通运动指令必须满足：已唤醒 + 语音模式
        with system_active_lock:
            system_active = SYSTEM_ACTIVE
        with mode_lock:
            mode = current_mode

        if system_active and mode == MODE_VOICE:
            msg_queue.put(MessageType.CONTROL_COMMAND, cmd_code)
            self.voice_feedback.play_command_voice(cmd_name, cmd_code)
            self.ignore_asr_until = time.time() + self.ECHO_SUPPRESS_SECONDS
            logger.info(f"发送语音控制指令: {cmd_code}")
            return True

        logger.info(f"识别到'{cmd_name}'，但系统未唤醒或当前不是语音模式，未发送控制指令")
        return False

    def head_pose_thread(self):
        """头部姿态控制线程。"""
        self.running = True
        logger.info("头部姿态控制启动")

        try:
            self.head_infer = HeadPoseYoloOpenCV(YOLO_MODEL_PATH, front_bias_threshold=0.1)
            logger.info("头部姿态模型加载成功")
        except Exception as e:
            logger.error(f"头部姿态模型加载失败: {e}")
            self.running = False
            return

        self.head_cap = None
        frame_counter = 0
        last_head_pose = None
        last_head_pose_time = 0
        HEAD_POSE_DEBOUNCE = 0.4
        logger.info("头部姿态控制就绪")

        try:
            while self.running:
                if not self.head_cap or not self.head_cap.isOpened():
                    logger.info("尝试打开摄像头")
                    self.head_cap = cv2.VideoCapture(CAMERA_ID, cv2.CAP_V4L2)
                    if self.head_cap.isOpened():
                        self.head_cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc("M", "J", "P", "G"))
                        self.head_cap.set(cv2.CAP_PROP_FRAME_WIDTH, MODEL_INPUT_SIZE)
                        self.head_cap.set(cv2.CAP_PROP_FRAME_HEIGHT, int(MODEL_INPUT_SIZE * 3 / 4))
                        self.head_cap.set(cv2.CAP_PROP_FPS, 10)
                        self.head_cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
                        logger.info("摄像头打开成功")
                    else:
                        logger.warning("无法打开摄像头，将在3秒后重试")
                        time.sleep(3)
                        continue

                frame_counter += 1
                if frame_counter % 20 == 0:
                    try:
                        self.head_cap.grab()
                        self.head_cap.grab()
                        ok, frame = self.head_cap.retrieve()
                        if not ok:
                            ok, frame = self.head_cap.read()
                        if ok:
                            dets = self.head_infer.infer(frame)
                            best = pick_best_detection(dets)

                            with mode_lock:
                                mode = current_mode
                            with system_active_lock:
                                active = SYSTEM_ACTIVE

                            if best is not None:
                                if mode == MODE_HEAD and active:
                                    current_time = time.time()
                                    # 检测到任何有效人脸/头部姿态都刷新头部模式计时，
                                    # 即使姿态没有变化、没有重新发送控制指令也会刷新。
                                    self.mark_face_activity(current_time)
                                    cmd = pose_to_char(best.class_name)
                                    if cmd != last_head_pose or (current_time - last_head_pose_time) > HEAD_POSE_DEBOUNCE:
                                        msg_queue.put(MessageType.CONTROL_COMMAND, cmd)
                                        self.voice_feedback.play_pose_voice(cmd)
                                        last_head_pose = cmd
                                        last_head_pose_time = current_time
                                        logger.info(f"头部姿态发送: {cmd} ({best.class_name})")
                                else:
                                    last_head_pose = None
                            else:
                                last_head_pose = None
                        else:
                            logger.warning("无法获取帧")
                    except Exception as e:
                        logger.error(f"读取摄像头失败: {e}")
                        if self.head_cap:
                            self.head_cap.release()
                            self.head_cap = None
                else:
                    try:
                        self.head_cap.grab()
                    except Exception as e:
                        logger.error(f"摄像头 grab 失败: {e}")
                        if self.head_cap:
                            self.head_cap.release()
                            self.head_cap = None

                time.sleep(0.01)

        except Exception as e:
            logger.error(f"头部姿态控制错误: {e}")
        finally:
            if self.head_cap:
                self.head_cap.release()
                logger.info("摄像头已关闭")
            logger.info("头部姿态控制已停止")

    def start(self):
        """启动集成控制模块。"""
        self.running = True

        self.voice_thread = threading.Thread(target=self.voice_recognition_thread, daemon=True)
        self.head_thread = threading.Thread(target=self.head_pose_thread, daemon=True)
        self.timeout_thread = threading.Thread(target=self.activity_watchdog_thread, daemon=True)
        self.voice_thread.start()
        self.head_thread.start()
        self.timeout_thread.start()

        logger.info("集成控制模块已启动")
        print("=" * 50)
        print("[系统] 集成控制模块已启动")
        print("[系统] 工作模式：拼音指令匹配 + 分模式空闲超时")
        print("[系统] 请说'开始'或'启动'来唤醒系统")
        print("=" * 50)

        self.start_speech_printing()

    def start_speech_printing(self):
        """启动语音打印线程。"""
        def print_worker():
            last_printed_text = ""
            last_print_time = 0.0
            PRINT_COOLDOWN = 0.3

            while self.running:
                speech = get_recent_speech()
                current_time = time.time()

                if speech and speech.strip():
                    if speech != last_printed_text and (current_time - last_print_time) >= PRINT_COOLDOWN:
                        print(f"[语音识别] 检测到语音: {speech}")
                        last_printed_text = speech
                        last_print_time = current_time
                else:
                    # 清空后允许下一次再次打印相同短命令。
                    last_printed_text = ""

                time.sleep(0.2)

        speech_thread = threading.Thread(target=print_worker, daemon=True)
        speech_thread.start()
        return speech_thread

    def stop(self):
        """停止集成控制模块。"""
        self.running = False
        if self.voice_thread:
            self.voice_thread.join(timeout=5)
        if self.head_thread:
            self.head_thread.join(timeout=5)
        if self.timeout_thread:
            self.timeout_thread.join(timeout=2)
        if self.head_cap:
            self.head_cap.release()
        logger.info("集成控制模块已停止")
        print("[系统] 集成控制模块已停止")


if __name__ == "__main__":
    ic = IntegratedControl()
    ic.start()
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        ic.stop()
