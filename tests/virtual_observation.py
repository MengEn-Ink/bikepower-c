"""虚拟设备观察窗：设备 uptime 定义时长，宿主 deadline 约束运行成本。"""
import asyncio
import math
import time


def require_frame_counts(left, right, window):
    minimum = math.ceil(window['device_seconds'] * 0.8)
    if min(left, right) < minimum:
        raise RuntimeError(f'三链路双下游通知不足 a={left} b={right} minimum={minimum}')
    return minimum


async def observe_device_time(read_clock, step, seconds, host_timeout,
                              *, now=time.monotonic, pause=asyncio.sleep):
    if type(seconds) is not int or not 1 <= seconds <= 120:
        raise ValueError('设备观察时长需在1..120秒')
    if not math.isfinite(host_timeout) or host_timeout <= 0:
        raise ValueError('宿主deadline必须为正有限值')
    started = now()
    deadline = started + host_timeout

    async def bounded(operation):
        remaining = deadline - now()
        if remaining <= 0:
            raise TimeoutError('宿主deadline耗尽，设备观察窗未完成')
        result = await asyncio.wait_for(operation(), remaining)
        if now() >= deadline:
            raise TimeoutError('宿主deadline耗尽，设备观察窗未完成')
        return result

    def validate(sample):
        boot, uptime = sample
        if any(type(value) is not int or not 0 <= value <= 0xffffffff for value in sample):
            raise ValueError('无效设备时钟')
        return boot, uptime

    boot, first = validate(await bounded(read_clock))
    previous = first
    iterations = 0
    while True:
        await bounded(lambda: step(iterations))
        await bounded(lambda: pause(1))
        current_boot, current = validate(await bounded(read_clock))
        if current_boot != boot:
            raise RuntimeError('观察期间设备重启，窗口失效')
        if current < previous:
            raise RuntimeError('设备uptime回退，窗口失效')
        previous = current
        iterations += 1
        elapsed = current - first
        if elapsed >= seconds:
            return {'clock': 'device_uptime', 'requested_seconds': seconds,
                    'device_seconds': elapsed, 'host_seconds': round(now()-started, 3),
                    'host_timeout_seconds': host_timeout, 'iterations': iterations}
