#!/bin/bash
# BikePower C — 测试运行器
# 用法: bash tests/test_runner.sh [options]
#   -p <port>   串口端口（真机测试所需）
#   -d          仅运行领域层 host 单测
#   -m          运行 mock 配网页保存测试（无需真机）
#   -e          运行 3 分钟核心流程耐久测试（无需真机）
#   -w          运行配网门户 HTTP 自测（需设备已在配网态）
#   -b          运行 BLE 自测（需真机）
#   -s          运行压力测试
#   -a          运行全部

set -euo pipefail
cd "$(dirname "$0")/.."

HOST_TEST="./test/test_domain"

# 领域层 host 单测
run_host_test() {
    echo "=== Host 单测 ==="
    make -C test
    $HOST_TEST
}

# Mock 配网页保存测试（无需真机）
run_mock_portal_test() {
    echo "=== Mock 配网页保存测试 ==="
    python3 tests/test_portal_config_save.py
}

# 3 分钟核心流程耐久测试（无需真机）
run_endurance_test() {
    echo "=== 核心流程耐久测试 ==="
    python3 tests/test_core_flow_endurance.py
}

# 配网门户 HTTP 自测（需设备已进入配网态）
run_portal_test() {
    echo "=== 配网门户 HTTP 自测 ==="
    python3 tests/stage5_portal_test.py
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

while getopts "p:dmewbasl" opt; do
    case $opt in
        p) PORT=$OPTARG ;;
        d) MODE="host" ;;
        m) MODE="mock" ;;
        e) MODE="endurance" ;;
        w) MODE="portal" ;;
        b) MODE="ble" ;;
        a) MODE="all" ;;
        s) MODE="stress" ;;
        l) MODE="log" ;;
        *) echo "用法: $0 [-p port] [-d|-m|-w|-b|-a|-s|-l]"; exit 1 ;;
    esac
done

if [ -z "$MODE" ]; then
    echo "指定测试模式: -d(host) -m(mock) -e(endurance) -w(portal) -b(ble) -a(all) -s(stress) -l(log)"
    exit 1
fi

case $MODE in
    host)
        run_host_test
        ;;
    mock)
        run_mock_portal_test
        ;;
    endurance)
        run_endurance_test
        ;;
    portal)
        run_portal_test
        ;;
    ble)
        run_ble_test
        ;;
    all)
        run_host_test
        run_mock_portal_test
        run_endurance_test
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
