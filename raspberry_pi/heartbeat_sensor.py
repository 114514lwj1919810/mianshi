#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
心跳检测模块
"""

import serial
import time
import threading
import glob
import sys
from config import HEARTBEAT_PORT, HEARTBEAT_BAUD
from utils.message_queue import telemetry_queue, MessageType
from utils.logger import get_logger

logger = get_logger(__name__)


class HeartbeatSensor:
    """心跳传感器"""
    def __init__(self):
        self.ser = None
        self.running = False
        self.thread = None

    def find_ch340_port(self):
        """查找CH340串口"""
        ports = glob.glob('/dev/ttyUSB*')
        if ports:
            logger.info(f"找到串口: {ports[0]}")
            return ports[0]
        else:
            logger.warning("未找到CH340串口")
            return None

    def init_sensor(self):
        """初始化传感器"""
        # 尝试查找串口
        port = self.find_ch340_port() or HEARTBEAT_PORT
        
        try:
            self.ser = serial.Serial(port, HEARTBEAT_BAUD, timeout=0.8)
            logger.info(f"心跳传感器连接成功: {port}")

            INIT_CMD = bytes.fromhex('FE 78 50 4B 00 00')
            self.ser.write(INIT_CMD)
            time.sleep(2)
            logger.info("心跳传感器初始化完成")
            return True
        except Exception as e:
            logger.error(f"心跳传感器初始化失败: {e}")
            self.ser = None
            return False

    def read_sensor_data(self):
        """读取传感器数据"""
        if not self.ser or not self.ser.is_open:
            logger.warning("传感器未打开，无法读取数据")
            return None

        try:
            READ_CMD = bytes.fromhex('FD 00 00 00 00 00')
            self.ser.write(READ_CMD)
            time.sleep(0.1)
            data = self.ser.read(128)  # 一次性读干净缓冲区

            for i in range(len(data)-5):
                raw = data[i:i+6]
                if len(raw) < 6:
                    continue

                if raw[0] == 0xFD:
                    h_bp, l_bp, hr = raw[1], raw[2], raw[3]
                    temp = ((raw[4] << 8) | raw[5]) / 256.0

                    if hr ==0 and h_bp ==0 and l_bp ==0:
                        note = "信号弱，继续按住"
                        logger.info(f"FD → {note} | 心率:{hr:3d} 血压:{h_bp:3d}/{l_bp:3d} 温度:{temp:.1f}°C")
                        return None
                    elif 0 < hr < 200 and 0 < h_bp < 200 and 0 < l_bp < 200:
                        note = "有效测量"
                        time.sleep(10)
                        logger.info(f"FD → {note} | 心率:{hr:3d} 血压:{h_bp:3d}/{l_bp:3d} 温度:{temp:.1f}°C")
                        heartbeat_data = {
                            "heart_rate": hr,
                            "blood_pressure_high": h_bp,
                            "blood_pressure_low": l_bp,
                            "temperature": temp
                        }
                        return heartbeat_data
                    else:
                        note = "无效值"
                        logger.info(f"FD → {note} | 心率:{hr:3d} 血压:{h_bp:3d}/{l_bp:3d} 温度:{temp:.1f}°C")
                        return None

        except Exception as e:
            logger.error(f"读取传感器数据失败: {e}")

        return None

    def run(self):
        """运行心跳传感器"""
        self.running = True
        logger.info("心跳传感器启动")

        logger.info("请用力且稳定按住传感器")

        last_data_time = time.time()
        last_print_time = time.time()

        while self.running:
            try:
                # 每10秒检查一次是否有数据
                current_time = time.time()
                if current_time - last_print_time >= 10:
                    if current_time - last_data_time >= 10:
                        logger.info("心跳模块未使用")
                    last_print_time = current_time

                # 初始化传感器（如果未初始化）
                if not self.ser or not self.ser.is_open:
                    logger.info("尝试初始化心跳传感器")
                    self.init_sensor()
                    time.sleep(2)
                else:
                    # 读取传感器数据
                    data = self.read_sensor_data()
                    if data:
                        # 数据已经在read_sensor_data方法中打印
                        # 将数据发送到消息队列，供OneNet控制模块上传
                        telemetry_queue.put(MessageType.HEARTBEAT_DATA, data)
                        last_data_time = current_time
                    time.sleep(3)  # 关键：放慢查询，让模块专心测量
            except Exception as e:
                logger.error(f"心跳传感器错误: {e}")
                # 尝试重新初始化传感器
                if not self.ser or not self.ser.is_open:
                    logger.info("尝试重新打开心跳传感器")
                    self.init_sensor()
                time.sleep(5)  # 出错后等待一段时间再重试

        # 清理
        if self.ser and self.ser.is_open:
            self.ser.close()
            logger.info("心跳传感器已关闭")

    def start(self):
        """启动心跳传感器线程"""
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()
        logger.info("心跳传感器线程已启动")

    def stop(self):
        """停止心跳传感器"""
        self.running = False
        if self.thread:
            self.thread.join(timeout=5)
        logger.info("心跳传感器已停止")


if __name__ == "__main__":
    sensor = HeartbeatSensor()
    sensor.start()
    
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        sensor.stop()
