#!/usr/bin/env python3
"""设备观察窗的停钟、回退、重启、宿主超时与帧数负例。"""
import asyncio
import unittest
from virtual_observation import observe_device_time, require_frame_counts


class ObservationTests(unittest.IsolatedAsyncioTestCase):
    async def run_window(self, clocks, seconds=3, timeout=12, counts=None):
        tick = [0.0]
        samples = iter(clocks)
        last = [None]
        async def read():
            try:
                last[0] = next(samples)
            except StopIteration:
                pass
            return last[0]
        async def step(index):
            if counts is not None:
                counts.append(index)
        async def pause(duration):
            tick[0] += duration
        return await observe_device_time(read, step, seconds, timeout,
                                         now=lambda: tick[0], pause=pause)

    async def test_slow_device_completes_only_device_window(self):
        result = await self.run_window([(1,10),(1,10),(1,11),(1,11),(1,12),(1,13)])
        self.assertEqual(result['device_seconds'],3)
        self.assertEqual(result['host_seconds'],5)
        self.assertEqual(result['iterations'],5)

    async def test_frozen_clock_fails_at_host_deadline(self):
        with self.assertRaises(TimeoutError):
            await self.run_window([(1,10)],timeout=4)

    async def test_clock_reversal_fails(self):
        with self.assertRaisesRegex(RuntimeError,'回退'):
            await self.run_window([(1,10),(1,11),(1,10)])

    async def test_reboot_even_with_increasing_uptime_fails(self):
        with self.assertRaisesRegex(RuntimeError,'重启'):
            await self.run_window([(1,10),(2,14)])

    async def test_invalid_clock_fails(self):
        for clock in ((1,-1),(1,True),(1,2**32)):
            with self.assertRaises(ValueError):
                await self.run_window([clock])

    async def test_hung_clock_read_is_bounded(self):
        async def read():
            await asyncio.Event().wait()
        async def step(_):
            pass
        with self.assertRaises(TimeoutError):
            await observe_device_time(read,step,3,0.02)

    async def test_hung_publish_is_bounded(self):
        async def read():
            return (1,1)
        async def step(_):
            await asyncio.Event().wait()
        with self.assertRaises(TimeoutError):
            await observe_device_time(read,step,3,0.02)

    async def test_cancel_is_not_success(self):
        async def read():
            raise asyncio.CancelledError()
        async def step(_):
            pass
        with self.assertRaises(asyncio.CancelledError):
            await observe_device_time(read,step,3,1)

    async def test_deadline_late_completion_rejected(self):
        with self.assertRaises(TimeoutError):
            await self.run_window([(1,10),(1,13)],timeout=1)

    async def test_jump_does_not_lower_frame_requirement(self):
        result = await self.run_window([(1,10),(1,20)])
        self.assertEqual(result['device_seconds'],10)
        self.assertEqual(require_frame_counts(8,8,result),8)
        with self.assertRaisesRegex(RuntimeError,'a=8 b=7'):
            require_frame_counts(8,7,result)

    async def test_missing_notifications_fail(self):
        window = {'device_seconds':15}
        for left,right in ((0,15),(15,0),(11,15)):
            with self.assertRaises(RuntimeError):
                require_frame_counts(left,right,window)
        self.assertEqual(require_frame_counts(12,12,window),12)

    async def test_invalid_window_parameters_fail(self):
        for seconds,timeout in ((0,10),(121,10),(True,10),(3,0),(3,float('inf'))):
            async def read():
                return (1,0)
            async def step(_):
                pass
            with self.assertRaises(ValueError):
                await observe_device_time(read,step,seconds,timeout)


if __name__ == '__main__':
    unittest.main()
