#!/usr/bin/env python3
"""测试进程信号前置与有界退出；不修改系统配置或其他进程。"""
import os
import signal
import subprocess


def prepare_test_signals():
    if os.name != 'posix':
        return False
    signal.signal(signal.SIGTERM, signal.SIG_DFL)
    signal.pthread_sigmask(signal.SIG_UNBLOCK, {signal.SIGTERM})
    return True


def stop_process(process, timeout=10):
    if process.poll() is not None:
        return process.wait(timeout=timeout)
    process.terminate()
    try:
        return process.wait(timeout=timeout)
    except subprocess.TimeoutExpired as error:
        process.kill()
        process.wait(timeout=timeout)
        raise RuntimeError('子进程未正常退出，已强制回收；状态落盘未获证明') from error
