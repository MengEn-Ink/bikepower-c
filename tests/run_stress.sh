#!/bin/bash
# BikePower C — 内存压力测试
# 持续运行 72h（或指定分钟数），监控串口日志中的堆使用/异常/重启
# 用法: bash tests/run_stress.sh [-m <minutes>] [-p <port>]

set -euo pipefail
cd "$(dirname "$0")/.."

DURATION=4320  # 默认 72h = 4320 分钟
PORT="/dev/cu.usbmodem101"
LOG_DIR="tests/logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_FILE="${LOG_DIR}/stress_${TIMESTAMP}.log"

while getopts "m:p:" opt; do
    case "$opt" in
        m) DURATION="$OPTARG" ;;
        p) PORT="$OPTARG" ;;
        *) echo "用法: bash tests/run_stress.sh [-m <minutes>] [-p <port>]"; exit 1 ;;
    esac
done

if ! [[ "$DURATION" =~ ^[0-9]+$ ]] || [ "$DURATION" -le 0 ]; then
    echo "错误: 测试时长必须是正整数分钟"
    exit 1
fi

mkdir -p "$LOG_DIR"

echo "=== BikePower C 老化测试 ==="
echo "时长: ${DURATION} 分钟"
echo "串口: ${PORT}"
echo "日志: ${LOG_FILE}"
echo "开始时间: $(date)"
echo ""

# 检查串口
if [ ! -c "$PORT" ]; then
    echo "错误: 串口 $PORT 不存在"
    echo "可用串口:"
    ls /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null || echo "(无)"
    exit 1
fi

# 启动日志收集（后台）
python3 tests/serial_capture.py -p "$PORT" -o "$LOG_FILE" -d $((DURATION * 60 + 60)) &
CAPTURE_PID=$!
echo "串口捕获进程 PID: $CAPTURE_PID"

# 主循环
HEARTBEAT_INTERVAL=60  # 每 60 分钟记录一次状态
if [ "$DURATION" -lt "$HEARTBEAT_INTERVAL" ]; then
    HEARTBEAT_INTERVAL="$DURATION"
fi
for ((i=0; i<DURATION; i+=HEARTBEAT_INTERVAL)); do
    sleep $((HEARTBEAT_INTERVAL * 60))
    ELAPSED=$((i + HEARTBEAT_INTERVAL))
    echo "[$(date)] 已运行 ${ELAPSED}/${DURATION} 分钟"

    if [ ! -s "$LOG_FILE" ]; then
        echo "错误: 串口日志为空，无法判定老化结果"
        exit 1
    fi

    # 检查串口捕获是否还在运行
    if [ "$ELAPSED" -lt "$DURATION" ] && ! kill -0 $CAPTURE_PID 2>/dev/null; then
        rc=0
        wait $CAPTURE_PID || rc=$?
        echo "错误: 串口捕获进程异常退出 rc=$rc"
        exit 1
    fi

    # 检查日志中是否有异常
    if grep -q "ERROR\|CRIT\|coredump\|panic\|abort\|restart" "$LOG_FILE" 2>/dev/null; then
        echo "⚠️ 检测到异常！请检查日志: $LOG_FILE"
        grep "ERROR\|CRIT\|coredump\|panic\|abort\|restart" "$LOG_FILE" | tail -5
    fi
done

# 停止串口捕获
kill $CAPTURE_PID 2>/dev/null || true
wait $CAPTURE_PID 2>/dev/null || true

echo "=== 老化测试完成 ==="
echo "结束时间: $(date)"
echo "日志文件: $LOG_FILE"

# 分析结果
if [ ! -s "$LOG_FILE" ]; then
    echo "状态: 失败（日志为空）"
    exit 1
elif grep -q "ERROR\|CRIT\|coredump\|panic\|abort\|restart" "$LOG_FILE"; then
    echo "⚠️ 测试期间检测到异常:"
    grep -c "ERROR" "$LOG_FILE" || echo "  无 ERROR"
    grep -c "CRIT" "$LOG_FILE" || echo "  无 CRIT"
    grep -c "restart" "$LOG_FILE" || echo "  无重启"
    echo "状态: 失败"
    exit 1
else
    echo "✅ 测试通过：无异常"
fi
