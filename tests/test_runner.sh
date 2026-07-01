#!/bin/bash
# BikePower C — 测试运行器
# 用法: bash tests/test_runner.sh [options]
#   -p <port>   串口端口（真机测试所需）
#   -d          仅运行领域层 host 单测
#   -b          运行 BLE 自测（需真机）
#   -s          运行压力测试
#   -a          运行全部

set -euo pipefail
cd "$(dirname "$0")/.."

HOST_TEST="./test/test_domain"

# 领域层 host 单测
run_host_test() {
    echo "=== Host 单测 ==="
    if [ ! -f "$HOST_TEST" ]; then
        echo "编译 host 单测..."
        make -C test
    fi
    $HOST_TEST
}

# BLE 自测（需真机 + 串口）
run_ble_test() {
    echo "=== BLE 自测 ==="
    python3 tests/ble_selftest.py
}

# 连接事件测试（需真机）
run_conn_test() {
    echo "=== 连接事件测试 ==="
    python3 tests/stage3_conn_test.py
}

# 压力测试
run_stress_test() {
    echo "=== 压力测试 ==="
    bash tests/run_stress.sh
}

# 日志收集
run_log() {
    echo "=== 串口日志 ==="
    python3 tests/serial_capture.py
}

# 主逻辑
PORT=""
MODE=""

while getopts "p:dbasl" opt; do
    case $opt in
        p) PORT=$OPTARG ;;
        d) MODE="host" ;;
        b) MODE="ble" ;;
        a) MODE="all" ;;
        s) MODE="stress" ;;
        l) MODE="log" ;;
        *) echo "用法: $0 [-p port] [-d|-b|-a|-s|-l]"; exit 1 ;;
    esac
done

if [ -z "$MODE" ]; then
    echo "指定测试模式: -d(host) -b(ble) -a(all) -s(stress) -l(log)"
    exit 1
fi

case $MODE in
    host)
        run_host_test
        ;;
    ble)
        run_ble_test
        ;;
    all)
        run_host_test
        run_ble_test
        run_conn_test
        echo "完成"
        ;;
    stress)
        run_stress_test
        ;;
    log)
        run_log
        ;;
esac
