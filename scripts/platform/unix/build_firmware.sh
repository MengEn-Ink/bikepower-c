#!/usr/bin/env bash
# BikePower C — 统一编译部署脚本
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
cd "$PROJECT_DIR"

IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}"
ACTION="${1:-build}"

if [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "错误: 找不到 ESP-IDF 环境脚本: $IDF_PATH/export.sh"
    exit 1
fi

source "$IDF_PATH/export.sh" > /dev/null 2>&1
if command -v ccache >/dev/null 2>&1; then
    export IDF_CCACHE_ENABLE=1
fi

case "$ACTION" in
    build)
        idf.py build
        ;;
    flash)
        PORT="${2:-}"
        if [ -z "$PORT" ]; then
            # 自动扫描
            for p in /dev/cu.usbmodem* /dev/cu.usbserial* /dev/ttyUSB* /dev/ttyACM*; do
                if [ -c "$p" ]; then
                    PORT="$p"
                    break
                fi
            done
        fi
        if [ -z "$PORT" ]; then
            echo "错误: 找不到串口设备"
            exit 1
        fi
        idf.py -p "$PORT" flash
        ;;
    monitor)
        PORT="${2:-}"
        if [ -z "$PORT" ]; then
            for p in /dev/cu.usbmodem* /dev/cu.usbserial* /dev/ttyUSB* /dev/ttyACM*; do
                if [ -c "$p" ]; then
                    PORT="$p"
                    break
                fi
            done
        fi
        if [ -z "$PORT" ]; then
            exec python3 scripts/tools/device_monitor.py
        else
            exec python3 scripts/tools/device_monitor.py -p "$PORT"
        fi
        ;;
    test)
        bash tests/test_runner.sh -d
        ;;
    clean)
        idf.py fullclean
        ;;
    *)
        echo "用法: $0 [build|flash|monitor|test|clean]"
        exit 1
        ;;
esac
