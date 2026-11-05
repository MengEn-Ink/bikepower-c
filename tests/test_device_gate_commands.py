#!/usr/bin/env python3
"""验证设备门禁显式板型传递；只收集编排命令，不操作设备。"""
import operation_gate


class Gate:
    def note_skip(self, *args):
        pass

    def record(self, *args, **kwargs):
        pass


def main():
    commands = []
    original = operation_gate.run_step
    try:
        operation_gate.run_step = lambda gate, name, command, *args, **kwargs: commands.append(command)
        for board in ("generic-c3", "m5stamp-c3"):
            for profile in ("cycling", "running"):
                commands.clear()
                operation_gate.device_steps(Gate(), "", False, profile, board)
                direct = [command for command in commands if "tests/ble_selftest.py" in command]
                assert len(direct) == 1
                assert direct[0].startswith("BIKEPOWER_EXPECT_BOARD=" + board + " "), direct[0]
                harness = [command for command in commands if "tests/device_profile_ble_harness.py" in command]
                assert "--board " + board in harness[0]
            commands.clear()
            operation_gate.device_steps(Gate(), "", True, "", board)
            portal = [command for command in commands if "tests/device_profile_harness.py" in command]
            assert len(portal) == 2
            assert all(command.startswith("BIKEPOWER_EXPECT_BOARD=" + board + " ") for command in portal), portal
        print("PASS 设备门禁：双板/双 profile 显式板型传递，不依赖根 sdkconfig")
    finally:
        operation_gate.run_step = original


if __name__ == "__main__":
    main()
