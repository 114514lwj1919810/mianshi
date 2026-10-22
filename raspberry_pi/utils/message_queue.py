#!/usr/bin/env python3
"""Typed queues used to decouple control and telemetry workers."""

from dataclasses import dataclass
from enum import Enum, auto
from queue import Empty, Full, Queue
from typing import Any, Optional

from config import QUEUE_SIZE


class MessageType(Enum):
    CONTROL_COMMAND = auto()
    HEARTBEAT_DATA = auto()


@dataclass(frozen=True)
class Message:
    msg_type: MessageType
    data: Any


class MessageQueue:
    def __init__(self, maxsize: int = QUEUE_SIZE):
        self._queue = Queue(maxsize=maxsize)

    def put(self, msg_type: MessageType, data: Any, block: bool = True) -> bool:
        try:
            self._queue.put(Message(msg_type, data), block=block, timeout=1 if block else None)
            return True
        except Full:
            return False

    def get(self, block: bool = True, timeout: Optional[float] = None) -> Optional[Message]:
        try:
            return self._queue.get(block=block, timeout=timeout)
        except Empty:
            return None


control_queue = MessageQueue()
telemetry_queue = MessageQueue()

# 兼容集成控制模块的原有接口；该别名只指向控制队列。
msg_queue = control_queue
