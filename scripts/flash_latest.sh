#!/usr/bin/env bash
# BikePower C — 烧录当前工作区最新版固件到 ESP32-C3 开发板
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

PORT="${BIKEPOWER_PORT:-}"
RUN_CHECKS=1
DO_BUILD=1
DO_ERASE=0
DO_MONITOR=0
CAPTURE_SECONDS=15
TEST_AUTO_CONFIG=OFF
STOP_DAEMON=1

usage() {
    cat <<'EOF'
用法: ./scripts/flash_latest.sh [选项]

一键编译并烧录当前工作区最新版 BikePower C 固件。

选项:
  -p, --port PORT             指定串口；默认自动识别 Espressif USB JTAG/serial
      --no-checks             跳过快速静态门禁
      --no-build              跳过显式 idf.py build，仅执行 flash
      --erase                 烧录前执行 erase-flash（首次从旧分区切到当前 4MB factory 布局时使用）
      --monitor               烧录后进入 idf.py monitor
      --no-capture            烧录后不抓取启动串口日志
      --capture-seconds N     烧录后抓取 N 秒启动日志，默认 15
      --portal-test-auto-config  编译配网自动触发测试固件；默认刷正式固件
      --keep-daemon           不自动停止 tests/serial_daemon.py
  -h, --help                  显示帮助

示例:
  ./scripts/flash_latest.sh
  ./scripts/flash_latest.sh --port /dev/ttyACM0 --monitor
  ./scripts/flash_latest.sh --erase
EOF
}

die() {
    echo "错误: $*" >&2
    exit 1
}

log() {
    echo ""
    echo "==> $*"
}

while [ $# -gt 0 ]; do
    case "$1" in
        -p|--port)
            [ $# -ge 2 ] || die "$1 需要端口参数"
            PORT="$2"
            shift 2
            ;;
        --port=*)
            PORT="${1#*=}"
            shift
            ;;
        --no-checks)
            RUN_CHECKS=0
            shift
            ;;
        --no-build)
            DO_BUILD=0
            shift
            ;;
        --erase)
            DO_ERASE=1
            shift
            ;;
        --monitor)
            DO_MONITOR=1
            shift
            ;;
        --no-capture)
            CAPTURE_SECONDS=0
            shift
            ;;
        --capture-seconds)
            [ $# -ge 2 ] || die "$1 需要秒数参数"
            CAPTURE_SECONDS="$2"
            shift 2
            ;;
        --capture-seconds=*)
            CAPTURE_SECONDS="${1#*=}"
            shift
            ;;
        --portal-test-auto-config)
            TEST_AUTO_CONFIG=ON
            shift
            ;;
        --keep-daemon)
            STOP_DAEMON=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage
            die "未知参数 $1"
            ;;
    esac
done

case "$CAPTURE_SECONDS" in
    ''|*[!0-9]*)
        die "--capture-seconds 必须是非负整数"
        ;;
esac

detect_idf_path() {
    local candidate
    local home_dir="${HOME:-}"
    local candidates=()

    if [ -n "${IDF_PATH:-}" ]; then
        candidates+=("$IDF_PATH")
    fi
    if [ -n "$home_dir" ]; then
        candidates+=(
            "$home_dir/esp/esp-idf-v5.5.4"
        )
    fi
    candidates+=(
        "$PROJECT_DIR/../esp32-power/esp-idf"
        "/home/mengen1014/work/esp32-power/esp-idf"
    )
    if [ -n "$home_dir" ]; then
        candidates+=("$home_dir/esp/esp-idf")
    fi

    for candidate in "${candidates[@]}"; do
        if [ -f "$candidate/export.sh" ]; then
            candidate="$(cd "$candidate" && pwd)"
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    if command -v idf.py >/dev/null 2>&1; then
        printf '\n'
        return 0
    fi

    return 1
}

activate_idf() {
    local idf_path="$1"
    if [ -n "$idf_path" ]; then
        export IDF_PATH="$idf_path"
        log "激活 ESP-IDF: $IDF_PATH"
        set +u
        # shellcheck disable=SC1090
        source "$IDF_PATH/export.sh" >/dev/null
        set -u
    else
        log "使用当前 shell 中已激活的 ESP-IDF"
    fi

    idf.py --version
}

detect_port() {
    local detected=""

    if command -v python3 >/dev/null 2>&1; then
        detected="$(python3 -c '
try:
    from serial.tools import list_ports
except Exception:
    raise SystemExit(0)

ports = list(list_ports.comports())

def score(port):
    device = port.device or ""
    hwid = (port.hwid or "").upper()
    desc = (port.description or "").lower()
    if "303A:1001" in hwid:
        return (0, device)
    if "ESP" in hwid or "espressif" in desc or "jtag/serial" in desc:
        return (1, device)
    if device.startswith("/dev/ttyACM") or device.startswith("/dev/ttyUSB"):
        return (2, device)
    if device.startswith("/dev/cu.usbmodem") or device.startswith("/dev/cu.usbserial"):
        return (3, device)
    return (9, device)

candidates = [port for port in ports if score(port)[0] < 9]
if candidates:
    print(sorted(candidates, key=score)[0].device)
' 2>/dev/null || true)"
    fi

    if [ -n "$detected" ]; then
        printf '%s\n' "$detected"
        return 0
    fi

    local p
    for p in /dev/ttyACM* /dev/ttyUSB* /dev/cu.usbmodem* /dev/cu.usbserial*; do
        if [ -c "$p" ]; then
            printf '%s\n' "$p"
            return 0
        fi
    done

    return 1
}

stop_project_serial_daemon() {
    local port="$1"
    [ "$STOP_DAEMON" -eq 1 ] || return 0

    local matches=""
    matches="$(pgrep -af "tests/serial_daemon.py" 2>/dev/null || true)"
    if [ -z "$matches" ]; then
        return 0
    fi

    local pids=""
    pids="$(printf '%s\n' "$matches" | grep -F -- "$port" | awk '{print $1}' || true)"
    if [ -z "$pids" ]; then
        return 0
    fi

    log "停止占用 $port 的 serial_daemon.py"
    printf '%s\n' "$pids" | xargs -r kill
    sleep 1
}

run_quick_checks() {
    local checks=(
        "python3 tests/check_portal_js.py"
        "python3 tests/check_config_consistency.py"
        "python3 tests/check_tasks_wdt.py"
        "python3 tests/check_nvs_schema.py"
        "python3 tests/check_board_desc.py"
    )
    local cmd

    log "运行快速静态门禁"
    for cmd in "${checks[@]}"; do
        echo "+ $cmd"
        bash -c "$cmd"
    done
}

IDF_DETECTED="$(detect_idf_path)" || die "找不到 ESP-IDF，请设置 IDF_PATH，或准备 $HOME/esp/esp-idf-v5.5.4、$PROJECT_DIR/../esp32-power/esp-idf、/home/mengen1014/work/esp32-power/esp-idf、$HOME/esp/esp-idf，或先激活 idf.py"
activate_idf "$IDF_DETECTED"

if [ -z "$PORT" ]; then
    PORT="$(detect_port)" || die "找不到串口设备，请用 --port 指定"
fi

[ -c "$PORT" ] || die "串口不存在或不是字符设备: $PORT"
log "使用串口: $PORT"

stop_project_serial_daemon "$PORT"

if [ "$RUN_CHECKS" -eq 1 ]; then
    run_quick_checks
else
    log "跳过快速静态门禁"
fi

if [ "$DO_BUILD" -eq 1 ]; then
    log "编译固件（PORTAL_TEST_AUTO_CONFIG=${TEST_AUTO_CONFIG}）"
    idf.py -DPORTAL_TEST_AUTO_CONFIG="${TEST_AUTO_CONFIG}" reconfigure
    idf.py -DPORTAL_TEST_AUTO_CONFIG="${TEST_AUTO_CONFIG}" build
else
    log "跳过显式编译"
fi

if [ "$DO_ERASE" -eq 1 ]; then
    log "擦除整片 flash"
    idf.py -p "$PORT" erase-flash
fi

log "烧录固件到 $PORT"
idf.py -p "$PORT" flash

if [ "$CAPTURE_SECONDS" -gt 0 ]; then
    mkdir -p tests/logs
    LOG_FILE="tests/logs/flash_latest_$(date +%Y%m%d_%H%M%S).log"
    log "抓取启动日志 ${CAPTURE_SECONDS}s: $LOG_FILE"
    python3 tests/serial_capture.py --port "$PORT" --duration "$CAPTURE_SECONDS" | tee "$LOG_FILE"
fi

if [ "$DO_MONITOR" -eq 1 ]; then
    log "进入串口监视器"
    exec idf.py -p "$PORT" monitor
fi

log "烧录完成"
