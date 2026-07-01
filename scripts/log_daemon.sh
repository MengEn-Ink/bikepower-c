#!/usr/bin/env bash
# ============================================================
#  BikePower C 串口日志守护进程 — 看门狗包装脚本
#
#  职责:
#    1. 后台启动 serial_daemon.py（--daemon 双 fork），持续采集串口日志
#    2. 看门狗：检测进程死亡后自动重启（最多 3 次/小时）
#    3. 提供 start/stop/status/logs 管理命令
#    4. 与 idf.py flash 烧录互斥：烧录期间 serial_daemon.py 主动暂停串口采集
#
#  用法:
#    bash scripts/log_daemon.sh start        # 启动守护进程（后台）
#    bash scripts/log_daemon.sh stop         # 停止守护进程
#    bash scripts/log_daemon.sh restart      # 重启守护进程
#    bash scripts/log_daemon.sh status       # 查看运行状态和最新日志
#    bash scripts/log_daemon.sh logs         # 查看当日日志
#    bash scripts/log_daemon.sh logs -f      # 实时跟踪日志（tail -f）
#    bash scripts/log_daemon.sh logs -c      # 查看最新日志（cat）
#    bash scripts/log_daemon.sh watchdog     # 启动看门狗（进程死亡后自动重启）
#    bash scripts/log_daemon.sh cleanup      # 清理旧日志（保留最近 5 个文件）
#
#  日志文件路径:
#    设备日志: tests/logs/device_YYYYMMDD_HHMMSS.log
#    守护日志: tests/logs/daemon.log
# ============================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DAEMON_PY="${PROJECT_DIR}/tests/serial_daemon.py"
PID_FILE="/tmp/bikepower_log_daemon.pid"
WATCHDOG_PID_FILE="/tmp/bikepower_watchdog.pid"
LOG_DIR="${PROJECT_DIR}/tests/logs"
RESTART_LOG="${LOG_DIR}/daemon_restart.log"
MAX_RESTARTS_PER_HOUR=3
# serial_daemon.py 的额外参数
DAEMON_ARGS="${DAEMON_ARGS:-}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

_log()   { echo -e "${CYAN}[DAEMON]${NC} $*"; }
_ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
_warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
_err()   { echo -e "${RED}[ERROR]${NC} $*"; }

is_macos() {
    [[ "$(uname -s)" == "Darwin" ]]
}

get_proc_info() {
    local pid="$1"
    if is_macos; then
        ps -p "$pid" -o pid,ppid,etime,command 2>/dev/null | tail -n +2 || echo "N/A"
    else
        ps -p "$pid" -o pid,ppid,etime,cmd --no-headers 2>/dev/null || echo "N/A"
    fi
}

list_device_logs_newest_first() {
    if is_macos; then
        find "$LOG_DIR" -name "device_*.log" -type f -exec stat -f '%m %N' {} + 2>/dev/null | sort -rn | cut -d' ' -f2-
    else
        find "$LOG_DIR" -name "device_*.log" -type f -printf '%T@ %p\n' 2>/dev/null | sort -rn | cut -d' ' -f2-
    fi
}

latest_device_log() {
    list_device_logs_newest_first | head -1
}

# ---- 获取守护进程 PID ----
get_pid() {
    if [[ -f "$PID_FILE" ]]; then
        local pid
        pid=$(head -1 "$PID_FILE" 2>/dev/null || true)
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            echo "$pid"
            return 0
        fi
    fi
    return 1
}

# ---- 记录重启 ----
log_restart() {
    mkdir -p "$LOG_DIR"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] 守护进程重启，PID=$1" >> "$RESTART_LOG"
}

# ---- 检查重启频率 ----
check_restart_rate() {
    local one_hour_ago
    if is_macos; then
        one_hour_ago=$(date -v-1H +%s 2>/dev/null || echo 0)
    else
        one_hour_ago=$(date -d '1 hour ago' +%s 2>/dev/null || echo 0)
    fi
    local count=0
    if [[ -f "$RESTART_LOG" ]]; then
        while IFS= read -r line; do
            local ts
            ts=$(echo "$line" | sed -n 's/^\[\([0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9] [0-9][0-9]:[0-9][0-9]:[0-9][0-9]\)\].*/\1/p')
            if [[ -n "$ts" ]]; then
                local epoch
                if is_macos; then
                    epoch=$(date -j -f '%Y-%m-%d %H:%M:%S' "$ts" +%s 2>/dev/null || echo 0)
                else
                    epoch=$(date -d "$ts" +%s 2>/dev/null || echo 0)
                fi
                if [[ "$epoch" -gt "$one_hour_ago" ]]; then
                    count=$((count + 1))
                fi
            fi
        done < "$RESTART_LOG"
    fi
    if [[ "$count" -ge "$MAX_RESTARTS_PER_HOUR" ]]; then
        _err "过去1小时内已重启 ${count} 次（上限 ${MAX_RESTARTS_PER_HOUR}），停止看门狗"
        return 1
    fi
    return 0
}

# ---- 启动守护进程 ----
do_start() {
    mkdir -p "$LOG_DIR"

    if get_pid > /dev/null; then
        _ok "守护进程已在运行 (PID=$(get_pid))"
        return 0
    fi

    if [[ ! -f "$DAEMON_PY" ]]; then
        _err "未找到 $DAEMON_PY"
        return 1
    fi

    _log "正在后台启动串口日志守护进程..."
    # serial_daemon.py --daemon 双 fork 后台化，自写 PID 文件 + fcntl 锁
    python3 "$DAEMON_PY" --daemon $DAEMON_ARGS

    sleep 2

    if get_pid > /dev/null; then
        _ok "守护进程已启动 (PID=$(get_pid))"
        log_restart "$(get_pid)"
        return 0
    else
        _err "守护进程启动失败，请检查串口是否可用"
        return 1
    fi
}

# ---- 停止守护进程 ----
do_stop() {
    local pid
    local stopped=false

    # 先停看门狗
    if [[ -f "$WATCHDOG_PID_FILE" ]]; then
        local wd_pid
        wd_pid=$(head -1 "$WATCHDOG_PID_FILE" 2>/dev/null || true)
        if [[ -n "$wd_pid" ]]; then
            kill "$wd_pid" 2>/dev/null || true
            rm -f "$WATCHDOG_PID_FILE"
            _log "看门狗已停止 (PID=$wd_pid)"
        fi
    fi

    if ! get_pid > /dev/null; then
        _ok "守护进程未运行"
        rm -f "$PID_FILE"
        return 0
    fi

    pid=$(get_pid)
    _log "正在停止守护进程 (PID=${pid})..."

    # SIGTERM 优雅退出
    kill "$pid" 2>/dev/null || true
    for i in $(seq 1 10); do
        if ! kill -0 "$pid" 2>/dev/null; then
            stopped=true
            break
        fi
        sleep 0.5
    done

    if [[ "$stopped" == false ]]; then
        _warn "优雅退出超时，强制终止..."
        kill -9 "$pid" 2>/dev/null || true
        sleep 1
    fi
    _ok "守护进程已停止"

    rm -f "$PID_FILE"
    return 0
}

# ---- 查看状态 ----
do_status() {
    local pid
    if get_pid > /dev/null; then
        pid=$(get_pid)
        _ok "守护进程运行中 (PID=${pid})"
        local proc_info
        proc_info=$(get_proc_info "$pid")
        echo "  $proc_info"

        local latest_log
        latest_log=$(latest_device_log || true)
        if [[ -n "$latest_log" ]]; then
            local log_size
            log_size=$(wc -c < "$latest_log" 2>/dev/null || echo 0)
            echo "  设备日志: $latest_log"
            echo "  日志大小: $log_size 字节 ($(echo "scale=1; $log_size/1024" | bc 2>/dev/null || echo "?") KB)"
            echo "  最后 5 行:"
            tail -5 "$latest_log" 2>/dev/null | while IFS= read -r line; do echo "    $line"; done
        else
            echo "  暂无设备日志"
        fi

        # 守护进程自身日志
        local daemon_log_file="${LOG_DIR}/daemon.log"
        if [[ -f "$daemon_log_file" ]]; then
            echo "  守护日志: $daemon_log_file ($(wc -c < "$daemon_log_file" 2>/dev/null || echo 0) 字节)"
        fi

        # 看门狗状态
        if [[ -f "$WATCHDOG_PID_FILE" ]]; then
            local wd_pid
            wd_pid=$(head -1 "$WATCHDOG_PID_FILE" 2>/dev/null || true)
            if [[ -n "$wd_pid" ]] && kill -0 "$wd_pid" 2>/dev/null; then
                echo "  看门狗: 运行中 (PID=$wd_pid)"
            else
                echo "  看门狗: 未运行"
            fi
        else
            echo "  看门狗: 未启用"
        fi
    else
        _warn "守护进程未运行"
        if [[ -f "$PID_FILE" ]]; then
            _warn "  PID 文件残留 (PID=$(head -1 "$PID_FILE"))"
        fi
        return 1
    fi
}

# ---- 查看日志 ----
do_logs() {
    local mode="${1:-cat}"
    local latest_log
    latest_log=$(latest_device_log || true)
    if [[ -z "$latest_log" ]]; then
        _warn "暂无设备日志文件"
        return 0
    fi
    _log "设备日志: $latest_log"
    echo "---"
    case "$mode" in
        -f) tail -f "$latest_log" ;;
        *)  cat "$latest_log" ;;
    esac
}

# ---- 看门狗模式 ----
do_watchdog() {
    echo "$$" > "$WATCHDOG_PID_FILE"
    _log "看门狗已启动 (PID=$$)，每 15 秒检查守护进程存活状态"

    while true; do
        sleep 15

        if ! get_pid > /dev/null; then
            _warn "守护进程已退出，检查是否需要重启..."
            if check_restart_rate; then
                _log "正在自动重启守护进程..."
                do_start || {
                    _err "自动重启失败"
                    continue
                }
            else
                _err "重启频率超限，看门狗退出"
                rm -f "$WATCHDOG_PID_FILE"
                exit 1
            fi
        fi
    done
}

# ---- 清理旧日志 (保留最近 5 个设备日志 + 清理过期守护日志) ----
do_cleanup() {
    local keep=5
    local count=0
    while IFS= read -r f; do
        count=$((count + 1))
        if [[ $count -gt $keep ]]; then
            rm -f "$f"
        fi
    done < <(list_device_logs_newest_first)
    # 清理超过 1 天的守护进程自身日志
    find "$LOG_DIR" -name "daemon.log" -type f -mtime +1 -delete 2>/dev/null || true
    find "$LOG_DIR" -name "daemon_restart.log" -type f -mtime +1 -delete 2>/dev/null || true
    _ok "已清理旧日志（保留最近 $keep 个设备日志文件）"
}

# ---- 主入口 ----
COMMAND="${1:-start}"
shift || true

case "$COMMAND" in
    start)
        do_start
        ;;
    stop)
        do_stop
        ;;
    restart)
        do_stop
        sleep 1
        do_start
        ;;
    status)
        do_status
        ;;
    logs)
        do_logs "$@"
        ;;
    watchdog)
        do_watchdog
        ;;
    cleanup)
        do_cleanup
        ;;
    *)
        echo "用法: bash scripts/log_daemon.sh {start|stop|restart|status|logs|watchdog|cleanup}"
        echo ""
        echo "  start       后台启动串口日志守护进程（--daemon 双 fork）"
        echo "  stop        停止守护进程和看门狗"
        echo "  restart     重启守护进程"
        echo "  status      查看运行状态、设备日志和看门狗状态"
        echo "  logs        查看最新设备日志全文"
        echo "  logs -f     实时跟踪日志（tail -f）"
        echo "  watchdog    启动看门狗（守护进程死亡后自动重启，上限 3 次/小时）"
        echo "  cleanup     清理旧日志（保留最近 5 个设备日志文件）"
        echo ""
        echo "日志文件:"
        echo "  设备日志: $LOG_DIR/device_YYYYMMDD_HHMMSS.log"
        echo "  守护日志: $LOG_DIR/daemon.log"
        echo ""
        echo "环境变量:"
        echo "  DAEMON_ARGS  传递给 serial_daemon.py 的额外参数"
        echo "               例: DAEMON_ARGS='--no-color' bash scripts/log_daemon.sh start"
        exit 1
        ;;
esac
