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

# Gitee raw 固件下载 URL 模板（不含 token；{name} 由产物文件名替换）
GITEE_RAW_BASE="https://gitee.com/mengen4jv/bikepower-c/raw/main/ota"

usage() {
    cat <<'EOF'
用法: ./scripts/build_all.sh [选项] [板型...]

双板型编译 BikePower C 固件，生成合并单文件固件包及 release 标准化产物。

板型（位置参数，可选；默认两种都编）:
  generic     仅编译通用 ESP32-C3
  m5stamp     仅编译 M5Stamp C3

选项:
      --dry-run   只打印将执行的版本、产物路径、将生成的 sha256/manifest/latest.json，
                  不编译、不写任何文件
  -h, --help      显示本帮助

输出物（写入 build/release/）:
  bikepower-c-<版本>-<板型>.bin         合并单文件固件（bootloader+分区表+app）
  bikepower-c-<版本>-<板型>.bin.sha256  对应 SHA256 校验文件
  manifest.txt                          产物清单（文件名/大小/SHA256/版本）
  latest.json                           OTA 元数据草案（changelog 为占位，需人工填写后才能发布）

说明:
  - 本脚本仅在本地生成产物，不会打 git tag、不会创建 Gitee Release、不做真机 OTA。
  - latest.json 的 url 使用 Gitee raw 模板（不含 token），changelog 字段为占位文本，
    发布前请人工替换为真实更新说明，并将固件与 latest.json 上传到 ota/ 目录。

示例:
  ./scripts/build_all.sh                # 编译两种板型并生成 release 产物
  ./scripts/build_all.sh generic        # 仅通用板
  ./scripts/build_all.sh --dry-run      # 预览将生成什么，不编译不写文件
EOF
}

# 脚本退出时自动清理临时配置文件（无论成功失败）
cleanup() {
    rm -f "$PROJECT_DIR/sdkconfig.board" "$PROJECT_DIR/sdkconfig.board.tmp"
}
trap cleanup EXIT

# 解析参数：分离 flag 与板型位置参数
DRY_RUN=0
POSITIONAL=()
while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "错误：未知选项 '$1'"; usage; exit 1 ;;
        *)  POSITIONAL+=("$1"); shift ;;
    esac
done

# 确定要编译的板型列表
if [ ${#POSITIONAL[@]} -gt 0 ]; then
    TARGETS=("${POSITIONAL[@]}")
else
    TARGETS=("generic" "m5stamp")
fi

# 校验板型合法性（提前拦截，dry-run 也校验）
for t in "${TARGETS[@]}"; do
    case "$t" in
        generic|m5stamp) ;;
        *) echo "错误：未知板型 '$t'，可选 generic / m5stamp"; exit 1 ;;
    esac
done

board_name_of() {
    case "$1" in
        generic) echo "generic-c3" ;;
        m5stamp) echo "m5stamp-c3" ;;
    esac
}

# 读取固件版本（dry-run 与正常路径共用）
VERSION="$(grep 'FIRMWARE_VERSION' "$PROJECT_DIR/main/bikepower_config.h" | head -1 | sed 's/.*"\(.*\)".*/\1/')"
if [ -z "$VERSION" ]; then
    echo "错误：无法从 main/bikepower_config.h 提取 FIRMWARE_VERSION"
    exit 1
fi

# 计算 SHA256（兼容 macOS shasum 与 Linux sha256sum）
sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

# dry-run：只打印计划，不编译不写文件
if [ "$DRY_RUN" -eq 1 ]; then
    echo "=== [dry-run] BikePower C Release 标准化预览 v${VERSION} ==="
    echo "不会编译、不会写文件、不会打 tag、不会创建 Release。"
    echo "板型: ${TARGETS[*]}"
    echo "输出目录: $RELEASE_DIR/"
    echo "将生成的产物:"
    for target in "${TARGETS[@]}"; do
        bn="$(board_name_of "$target")"
        fw="bikepower-c-${VERSION}-${bn}.bin"
        echo "  - $fw            （合并单文件固件）"
        echo "  - ${fw}.sha256   （SHA256 校验）"
    done
    echo "  - manifest.txt     （产物清单：文件名/大小/SHA256/版本）"
    echo "  - latest.json      （OTA 元数据草案，changelog 为占位待人工填写）"
    first_bn="$(board_name_of "${TARGETS[0]}")"
    echo "latest.json.url 模板示例: ${GITEE_RAW_BASE}/bikepower-c-${VERSION}-${first_bn}.bin"
    exit 0
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

echo "=== BikePower C 双板型构建 v${VERSION} ==="

mkdir -p "$RELEASE_DIR"

# 记录本次生成的固件产物（用于后续 SHA256 / manifest / latest.json）
PRODUCED_BINS=()

for target in "${TARGETS[@]}"; do
    board_name="$(board_name_of "$target")"
    case "$target" in
        generic) kconfig="CONFIG_BOARD_GENERIC_C3=y" ;;
        m5stamp) kconfig="CONFIG_BOARD_M5STAMP_C3=y" ;;
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
    PRODUCED_BINS+=("$FW_NAME")

    # 清理临时文件
    rm -f sdkconfig.board sdkconfig.board.tmp
done

# ============================ Release 标准化 ============================
# 对每个固件产物生成 SHA256 旁文件，并汇总产物清单与 OTA 元数据草案。
echo ""
echo "=== Release 标准化：SHA256 / manifest / latest.json ==="

MANIFEST="$RELEASE_DIR/manifest.txt"
{
    echo "# BikePower C Release 产物清单"
    echo "# 版本: v${VERSION}"
    echo "# 生成时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "# 字段: 文件名  大小(字节)  SHA256"
} > "$MANIFEST"

for fw in "${PRODUCED_BINS[@]}"; do
    fw_path="$RELEASE_DIR/$fw"
    sum="$(sha256_of "$fw_path")"
    size="$(wc -c < "$fw_path" | tr -d ' ')"
    # SHA256 旁文件（格式兼容 sha256sum -c）
    echo "${sum}  ${fw}" > "${fw_path}.sha256"
    echo "${fw}  ${size}  ${sum}" >> "$MANIFEST"
    echo "  SHA256: $fw -> ${sum:0:16}..."
done

# 版本号一致性校验：产物文件名必须含当前 FIRMWARE_VERSION
for fw in "${PRODUCED_BINS[@]}"; do
    case "$fw" in
        *"-${VERSION}-"*) ;;
        *) echo "错误：产物名 '$fw' 与版本 v${VERSION} 不一致"; exit 1 ;;
    esac
done

# 生成 latest.json 草案（字段对齐 ota_downloader：version/url/size/changelog）
# 默认指向第一个板型固件；changelog 为占位，需人工填写后才能发布。
PRIMARY_FW="${PRODUCED_BINS[0]}"
PRIMARY_PATH="$RELEASE_DIR/$PRIMARY_FW"
PRIMARY_SIZE="$(wc -c < "$PRIMARY_PATH" | tr -d ' ')"
LATEST_JSON="$RELEASE_DIR/latest.json"
cat > "$LATEST_JSON" <<JSONEOF
{
  "version": "${VERSION}",
  "url": "${GITEE_RAW_BASE}/${PRIMARY_FW}",
  "size": ${PRIMARY_SIZE},
  "changelog": "TODO: 发布前请人工填写本版本更新说明"
}
JSONEOF

# 校验 latest.json 中 version 与产物一致
if ! grep -q "\"version\": \"${VERSION}\"" "$LATEST_JSON"; then
    echo "错误：latest.json version 与 v${VERSION} 不一致"
    exit 1
fi

echo ""
echo "=== 构建完成，产物在 $RELEASE_DIR/ ==="
ls -lh "$RELEASE_DIR/"
echo ""
echo "⚠️  发布前人工步骤（本脚本不自动执行）："
echo "   1. 编辑 $LATEST_JSON 的 changelog 为真实更新说明"
echo "   2. 确认 url 指向的板型固件正确（默认 ${PRIMARY_FW}）"
echo "   3. 将固件与 latest.json 上传到仓库 ota/ 目录后再发布"
echo "   本脚本不会打 git tag、不会创建 Gitee Release。"
