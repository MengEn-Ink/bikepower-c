#!/bin/bash
# BikePower C — 内存压力测试
# 持续运行 72h（或指定分钟数），监控串口日志中的堆使用/异常/重启
# 用法: bash tests/run_stress.sh [-m <minutes>] [-p <port>]

set -euo pipefail

DURATION=${1:-4320}  # 默认 72h = 4320 分钟
PORT=${2:-"/dev/cu.usbmodem101"}
LOG_DIR="tests/logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_FILE="${LOG_DIR}/stress_${TIMESTAMP}.log"

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
python3 tests/serial_capture.py -p "$PORT" -o "$LOG_FILE" &
CAPTURE_PID=$!
echo "串口捕获进程 PID: $CAPTURE_PID"

# 主循环
HEARTBEAT_INTERVAL=60  # 每 60 分钟记录一次状态
for ((i=0; i<DURATION; i+=HEARTBEAT_INTERVAL)); do
    sleep $((HEARTBEAT_INTERVAL * 60))
    ELAPSED=$((i + HEARTBEAT_INTERVAL))
    echo "[$(date)] 已运行 ${ELAPSED}/${DURATION} 分钟"

    # 检查串口捕获是否还在运行
    if ! kill -0 $CAPTURE_PID 2>/dev/null; then
        echo "警告: 串口捕获进程已退出，重启..."
        python3 tests/serial_capture.py -p "$PORT" -o "$LOG_FILE" &
        CAPTURE_PID=$!
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
if grep -q "ERROR\|CRIT\|coredump\|panic\|abort\|restart" "$LOG_FILE" 2>/dev/null; then
    echo "⚠️ 测试期间检测到异常:"
    grep -c "ERROR" "$LOG_FILE" || echo "  无 ERROR"
    grep -c "CRIT" "$LOG_FILE" || echo "  无 CRIT"
    grep -c "restart" "$LOG_FILE" || echo "  无重启"
    echo "状态: 失败"
    exit 1
else
    echo "✅ 测试通过：无异常"
fi
