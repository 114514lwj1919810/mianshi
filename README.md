# 多模态智能轮椅控制系统

基于 Raspberry Pi 4B 与 STM32F407 的边缘智能控制项目。树莓派负责离线语音识别、头部姿态推理、MQTT/OneNET 通信和任务调度；STM32 负责 USART 指令接收、ADC+DMA 摇杆采样、人工优先仲裁与双路 DAC 输出。

## 技术栈

- Raspberry Pi/Linux、Python、多线程、消息队列
- Vosk、PyAudio、pypinyin
- OpenCV DNN、YOLO、ONNX
- MQTT、OneNET、uni-app/微信小程序
- STM32F407、C/HAL、USART、ADC+DMA、DAC

## 目录

```text
raspberry_pi/  树莓派端运行代码
stm32/         STM32CubeIDE/CubeMX 工程
miniprogram/   uni-app 微信小程序源码
models/        头部姿态 ONNX 模型
docs/          项目与面试讲解文档
```

## 树莓派端运行

1. 安装系统依赖（Raspberry Pi OS）：

```bash
sudo apt update
sudo apt install python3-pip python3-opencv portaudio19-dev libopenblas-dev
python3 -m pip install -r requirements.txt
```

2. 下载 `vosk-model-small-cn-0.22`，解压到 `models/`，或通过环境变量指定绝对路径。

3. 复制 `.env.example` 中的配置到本地启动环境。真实 OneNET AccessKey/token 不要提交到仓库。

4. 确认摄像头、麦克风、`/dev/ttyAMA0` 和 `/dev/ttyUSB0` 权限后启动：

```bash
cd raspberry_pi
python3 start_all.py
```

## 控制协议

树莓派通过 115200、8N1 USART 向 STM32 发送单字节 ASCII 控制码：`F/B/L/R/S/A/D`。STM32 使用中断接收；摇杆偏离死区时人工输入优先，否则使用上位机控制码生成 DAC 输出。

## 模型部署说明

`models/head_pose.onnx` 由 OpenCV DNN 在 CPU 上加载，输入为 128×128，输出形状为 `[1, 9, 336]`。类别顺序必须与模型元数据保持一致：

```text
front, right, up, left, down
```

## 安全说明

- 仓库中的云端凭据均已替换为占位符。
- Vosk 中文模型体积较大，未纳入 Git；请按上述步骤单独下载。
- 项目包含应用层超时停止机制；实际设备还建议在 STM32 端增加通信心跳超时、硬件看门狗和物理急停。

更详细的架构、实现原理和面试问答见 `docs/面试讲解-多模态智能轮椅控制系统.md`。
