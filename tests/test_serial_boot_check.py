#!/usr/bin/env python3
"""Tests for serial boot log acceptance checks."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests"))

from serial_capture import analyze_boot_log  # noqa: E402

TAG = "[serial-boot-check-test]"


def main():
    healthy = "\n".join(
        [
            "ESP-ROM:esp32c3-api1-20210207",
            "Build:Feb  7 2021",
            "rst:0x15 (USB_UART_CHIP_RESET),boot:0xc (SPI_FAST_FLASH_BOOT)",
            "entry 0x403cbf10",
        ]
    )
    ok, detail = analyze_boot_log(healthy)
    assert ok, detail
    assert "ROM 复位启动" in detail

    ok, detail = analyze_boot_log("")
    assert not ok
    assert "为空" in detail

    ok, detail = analyze_boot_log("ESP-ROM:esp32c3-api1-20210207")
    assert not ok
    assert "rst:" in detail

    crash = healthy + "\nGuru Meditation Error: Core 0 panic'ed\nBacktrace: 0x1234"
    ok, detail = analyze_boot_log(crash)
    assert not ok
    assert "Guru Meditation" in detail

    wdt = healthy + "\nE task_wdt: Task watchdog got triggered"
    ok, detail = analyze_boot_log(wdt)
    assert not ok
    assert "Task watchdog" in detail

    partition_table = "\n".join(
        [
            healthy,
            "I (46) boot: ## Label            Usage          Type ST Offset   Length",
            "I (72) boot:  2 factory          factory app      00 00 00010000 003e0000",
            "I (79) boot:  3 coredump         Unknown data     01 03 003f0000 00010000",
            "I (82) boot: End of partition table",
        ]
    )
    ok, detail = analyze_boot_log(partition_table)
    assert ok, f"分区表 coredump 标签不应误报致命异常: {detail}"

    real_coredump = healthy + "\nI (999) esp_core_dump_flash: Saving core dump to flash..."
    ok, detail = analyze_boot_log(real_coredump)
    assert not ok
    assert "Saving core dump" in detail

    print(f"{TAG} PASS: serial boot log checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
