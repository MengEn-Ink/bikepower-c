#!/bin/bash
# BikePower C — 双板型构建脚本
# 分别编译通用 ESP32-C3 和 M5Stamp C3 固件，
# 输出合并后的单文件固件包，包名包含板型名称和版本号。
#
# 用法：
#   ./scripts/build_all.sh              # 编译两种板型
#   ./scripts/build_all.sh generic      # 仅编译通用板
#   ./scripts/build_all.sh m5stamp      # 仅编译 M5Stamp C3
#
# 产物输出到 build/release/ 目录。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RELEASE_DIR="$PROJECT_DIR/build/release"
# 对齐build_firmware.sh逻辑：优先使用环境变量IDF_PATH，默认macOS路径
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}"

# 脚本退出时自动清理临时配置文件（无论成功失败）
cleanup() {
    rm -f "$PROJECT_DIR/sdkconfig.board" "$PROJECT_DIR/sdkconfig.board.tmp"
}
trap cleanup EXIT

# 确定要编译的板型列表
if [ $# -gt 0 ]; then
    TARGETS=("$@")
else
    TARGETS=("generic" "m5stamp")
fi

# 激活 ESP-IDF 环境
if [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "错误：找不到ESP-IDF环境，$IDF_PATH/export.sh 不存在"
    echo "请设置IDF_PATH环境变量指向ESP-IDF v5.5.x安装目录，或安装到默认路径 $HOME/esp/esp-idf-v5.5.4"
    exit 1
fi
echo "激活ESP-IDF环境: $IDF_PATH"
source "$IDF_PATH/export.sh" > /dev/null

cd "$PROJECT_DIR"

# 读取固件版本
VERSION=$(grep 'FIRMWARE_VERSION' main/bikepower_config.h | head -1 | sed 's/.*"\(.*\)".*/\1/')
echo "=== BikePower C 双板型构建 v${VERSION} ==="

mkdir -p "$RELEASE_DIR"

for target in "${TARGETS[@]}"; do
    case "$target" in
        generic)
            board_name="generic-c3"
            kconfig="CONFIG_BOARD_GENERIC_C3=y"
            ;;
        m5stamp)
            board_name="m5stamp-c3"
            kconfig="CONFIG_BOARD_M5STAMP_C3=y"
            ;;
        *)
            echo "错误：未知板型 '$target'，可选 generic / m5stamp"
            exit 1
            ;;
    esac

    echo ""
    echo "--- 构建板型: ${board_name} ---"

    # 生成 sdkconfig.defaults 合并文件（追加板型选择）
    echo "$kconfig" > sdkconfig.board
    cat sdkconfig.defaults sdkconfig.board > sdkconfig.board.tmp

    # 删除旧 sdkconfig 强制从 defaults 重新生成
    rm -f sdkconfig

    # 编译
    idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.board.tmp" build

    # 合并固件为单文件
    FW_NAME="bikepower-c-${VERSION}-${board_name}.bin"
    python3 "$IDF_PATH/components/esptool_py/esptool/esptool.py" \
        --chip esp32c3 merge_bin \
        -o "$RELEASE_DIR/$FW_NAME" \
        --flash_mode dio --flash_freq 80m --flash_size 2MB \
        0x0 build/bootloader/bootloader.bin \
        0x8000 build/partition_table/partition-table.bin \
        0x10000 build/bikepower-c.bin

    echo "  产物: $RELEASE_DIR/$FW_NAME"
    ls -lh "$RELEASE_DIR/$FW_NAME" | awk '{print "  大小: "$5}'

    # 清理临时文件
    rm -f sdkconfig.board sdkconfig.board.tmp
done

echo ""
echo "=== 构建完成，产物在 $RELEASE_DIR/ ==="
ls -lh "$RELEASE_DIR/"
