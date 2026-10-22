#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
启动所有模块
"""

import sys
import os
import time
import pygame

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from serial_controller import SerialController
from heartbeat_sensor import HeartbeatSensor
from integrated_control import IntegratedControl
from onenet_control import OneNetControl
from utils.logger import get_logger
from config import AUDIO_DIR

logger = get_logger(__name__)

def play_welcome():
    """播放欢迎语音"""
    logger.info("开始执行 play_welcome 函数")
    try:
        logger.info(f"当前 AUDIO_DIR: {AUDIO_DIR}")
        welcome_file = os.path.join(AUDIO_DIR, "欢迎.mp3")
        logger.info(f"尝试播放欢迎语音: {welcome_file}")
        if os.path.exists(welcome_file):
            logger.info(f"文件大小: {os.path.getsize(welcome_file)} 字节")
            pygame.mixer.music.load(welcome_file)
            logger.info("音乐加载成功")
            pygame.mixer.music.play()
            logger.info("🔊 播放欢迎语音")
        else:
            logger.warning(f"欢迎语音文件不存在: {welcome_file}")
    except Exception as e:
        logger.error(f"播放欢迎语音失败: {e}")
        import traceback
        logger.error(f"异常堆栈: {traceback.format_exc()}")
    logger.info("play_welcome 函数执行完成")


def main():
    """主函数"""
    logger.info("开始启动所有模块")
    
    # 创建模块实例
    serial_controller = SerialController()
    heartbeat_sensor = HeartbeatSensor()
    integrated_control = IntegratedControl()
    onenet_control = OneNetControl()
    
    # 启动模块（顺序很重要）
    logger.info("启动串口控制模块")
    serial_controller.start()
    time.sleep(2)
    
    logger.info("启动心跳检测模块")
    heartbeat_sensor.start()
    time.sleep(2)
    
    logger.info("启动集成控制模块")
    integrated_control.start()
    time.sleep(2)
    
    logger.info("启动OneNet控制模块")
    onenet_control.start()
    time.sleep(2)
    
    logger.info("所有模块启动完成")
    logger.info("准备调用 play_welcome 函数")
    play_welcome()
    logger.info("系统已就绪，按 Ctrl+C 退出")
    
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        logger.info("收到中断信号，正在停止所有模块...")
        
        # 停止模块（顺序相反）
        onenet_control.stop()
        integrated_control.stop()
        heartbeat_sensor.stop()
        serial_controller.stop()
        
        logger.info("所有模块已停止")


if __name__ == "__main__":
    main()