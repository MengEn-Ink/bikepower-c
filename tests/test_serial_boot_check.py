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

    print(f"{TAG} PASS: serial boot log checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
