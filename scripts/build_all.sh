#!/bin/bash
# BikePower C — 双板型构建脚本
# 分别编译通用 ESP32-C3 和 M5Stamp C3 固件，
# 输出合并后的单文件固件包，包名包含板型、单一版本号、stable/fix 标识和 Git ID。
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
DEVICE_ACCEPTANCE_DIR="${DEVICE_ACCEPTANCE_DIR:-$PROJECT_DIR/tests/reports}"
# 对齐build_firmware.sh逻辑：优先使用环境变量IDF_PATH，默认macOS路径
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}"

usage() {
    cat <<'EOF'
用法: ./scripts/build_all.sh [选项] [板型...]

双板型编译 BikePower C 固件，生成合并单文件固件包及 release 标准化产物。

板型（位置参数，可选；默认两种都编）:
  generic     仅编译通用 ESP32-C3
  m5stamp     仅编译 M5Stamp C3

选项:
      --dry-run   只打印将执行的版本、产物路径、将生成的 sha256/manifest，
                  不编译、不写任何文件
  -h, --help      显示本帮助

输出物（写入 build/release/）:
  bikepower-c-<板型>-v<版本>-stable-fix-g<GitID>.bin
                                        合并单文件固件（bootloader+分区表+app）
  *.bin.sha256                         对应 SHA256 校验文件
  manifest.txt                         产物清单（文件名/大小/SHA256/版本）
  test-report.md                       本次固件测试报告
  core-flow-endurance.md               3 分钟核心流程耐久测试明细
  user-manual.md                       随固件分发的用户文档副本
  images/                              用户文档引用的截图与示意图
  bikepower-c-release-v<版本>-stable-fix-g<GitID>.zip
                                      双板型固件、报告、手册、截图的总包
  *.zip.sha256                         总包 SHA256 校验文件
  evidence/device-acceptance-<板型>.json
                                        当前版本各板型真机验收证据（存在时收录）

说明:
  - 本脚本仅在本地生成产物，不会打 git tag、不会创建 GitHub Release。
  - 永久放弃设备内 OTA，普通用户升级统一走网页端在线烧录（见 README）。

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

# 读取固件版本元信息（dry-run 与正常路径共用）
extract_define() {
    local name="$1"
    local value
    value="$(awk -v name="$name" '$1 == "#define" && $2 == name {print $0; exit}' "$PROJECT_DIR/main/bikepower_config.h" | sed 's/^[^"]*"\([^"]*\)".*$/\1/')"
    printf '%s\n' "$value"
}

VERSION="$(extract_define FIRMWARE_VERSION)"
STABLE_VERSION="$(extract_define FIRMWARE_STABLE_VERSION)"
FIX_VERSION="$(extract_define FIRMWARE_FIX_VERSION)"
GIT_SHA="$(git -C "$PROJECT_DIR" rev-parse --short HEAD 2>/dev/null || true)"
if [ -z "$GIT_SHA" ]; then
    GIT_SHA="nogit"
fi

if [ -z "$VERSION" ] || [ -z "$STABLE_VERSION" ] || [ -z "$FIX_VERSION" ]; then
    echo "错误：无法从 main/bikepower_config.h 提取完整固件版本元信息"
    exit 1
fi

firmware_file_name() {
    local board_name="$1"
    printf 'bikepower-c-%s-v%s-stable-fix-g%s.bin\n' \
        "$board_name" "$VERSION" "$GIT_SHA"
}

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
    echo "稳定版: ${STABLE_VERSION}"
    echo "修复版: ${FIX_VERSION}"
    echo "Git ID: ${GIT_SHA}"
    echo "板型: ${TARGETS[*]}"
    echo "输出目录: $RELEASE_DIR/"
    echo "将生成的产物:"
    for target in "${TARGETS[@]}"; do
        bn="$(board_name_of "$target")"
        fw="$(firmware_file_name "$bn")"
        echo "  - $fw            （合并单文件固件）"
        echo "  - ${fw}.sha256   （SHA256 校验）"
    done
    echo "  - manifest.txt     （产物清单：文件名/大小/SHA256/版本）"
    echo "  - test-report.md   （测试报告）"
    echo "  - core-flow-endurance.md （3 分钟核心流程耐久测试明细）"
    echo "  - user-manual.md   （用户文档副本）"
    echo "  - images/          （用户文档截图与示意图）"
    echo "  - evidence/device-acceptance-<板型>.json （当前版本各板型真机验收证据；存在时收录）"
    echo "  - bikepower-c-release-v${VERSION}-stable-fix-g${GIT_SHA}.zip （release 总包）"
    echo "  - bikepower-c-release-v${VERSION}-stable-fix-g${GIT_SHA}.zip.sha256"
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

echo "=== BikePower C 双板型构建 v${VERSION} stable${STABLE_VERSION} fix${FIX_VERSION} g${GIT_SHA} ==="

mkdir -p "$RELEASE_DIR"
rm -f "$RELEASE_DIR"/bikepower-c-*.bin \
      "$RELEASE_DIR"/bikepower-c-*.bin.sha256 \
      "$RELEASE_DIR"/manifest.txt \
      "$RELEASE_DIR"/test-report.md \
      "$RELEASE_DIR"/core-flow-endurance.md \
      "$RELEASE_DIR"/user-manual.md \
      "$RELEASE_DIR"/bikepower-c-release-*.zip \
      "$RELEASE_DIR"/bikepower-c-release-*.zip.sha256
rm -rf "$RELEASE_DIR/evidence"

echo ""
echo "=== 构建前测试门禁：host / mock / static ==="
make -C test
./test/build/test_domain
python3 tests/test_portal_config_save.py
python3 tests/test_core_flow_endurance.py \
    --minutes "${CORE_FLOW_MINUTES:-3}" \
    --report "$RELEASE_DIR/core-flow-endurance.md"
python3 tests/check_config_consistency.py
python3 tests/check_portal_js.py
python3 tests/check_portal_structure.py
python3 tests/check_nvs_schema.py
python3 tests/check_tasks_wdt.py
python3 tests/check_board_desc.py
python3 tests/check_ble_advertising_fields.py
python3 tests/test_ble_protocol_vectors.py
python3 tests/test_ble_reconnect_harness.py
python3 tests/test_ble_running_harness.py
python3 tests/test_device_profile_harness.py
python3 tests/check_wifi_done_contract.py
python3 tests/check_ftms_control_contract.py

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
    FW_NAME="$(firmware_file_name "$board_name")"
    python3 "$IDF_PATH/components/esptool_py/esptool/esptool.py" \
        --chip esp32c3 merge_bin \
        -o "$RELEASE_DIR/$FW_NAME" \
        --flash_mode dio --flash_freq 80m --flash_size 4MB \
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
# 对每个固件产物生成 SHA256 旁文件，并汇总产物清单。
echo ""
echo "=== Release 标准化：SHA256 / manifest ==="

MANIFEST="$RELEASE_DIR/manifest.txt"
{
    echo "# BikePower C Release 产物清单"
    echo "# 版本: v${VERSION}"
    echo "# 稳定版: ${STABLE_VERSION}"
    echo "# 修复版: ${FIX_VERSION}"
    echo "# Git ID: ${GIT_SHA}"
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
    sha_file="${fw}.sha256"
    sha_path="$RELEASE_DIR/$sha_file"
    sha_sum="$(sha256_of "$sha_path")"
    sha_size="$(wc -c < "$sha_path" | tr -d ' ')"
    echo "${sha_file}  ${sha_size}  ${sha_sum}" >> "$MANIFEST"
    echo "  SHA256: $fw -> ${sum:0:16}..."
done

echo ""
echo "=== 真机验收证据：按板型校验并收录 ==="
EVIDENCE_COUNT=0
for target in "${TARGETS[@]}"; do
    board_name="$(board_name_of "$target")"
    evidence_name="device-acceptance-${board_name}.json"
    evidence_src="${DEVICE_ACCEPTANCE_DIR}/${evidence_name}"
    if [ -f "$evidence_src" ]; then
        python3 "$PROJECT_DIR/scripts/tools/device_acceptance.py" validate \
            --file "$evidence_src" \
            --firmware-version "$VERSION" \
            --git-sha "$GIT_SHA" \
            --board "$board_name"
        mkdir -p "$RELEASE_DIR/evidence"
        cp "$evidence_src" "$RELEASE_DIR/evidence/$evidence_name"
        echo "  已收录: evidence/$evidence_name"
        EVIDENCE_COUNT=$((EVIDENCE_COUNT + 1))
    else
        echo "  未找到 ${evidence_src} (允许本地构建，正式发布前必须补齐该板型全部 P0 证据)"
    fi
done

TEST_SUMMARY="构建前门禁已通过：host 领域层单测；BLE 协议 golden vectors；BLE 重连/running/profile harness；WiFi done/FTMS 控制点契约；门户 mock 与核心流程耐久；配置、门户、NVS、任务/WDT、板型及 BLE 广播静态检查。真机烧录、BLE 自测、配网自测和物理按钮/LED 验证需按测试报告补充执行记录。"
python3 "$PROJECT_DIR/scripts/tools/generate_release_docs.py" \
    --release-dir "$RELEASE_DIR" \
    --test-summary "$TEST_SUMMARY" \
    --notes "build_all.sh 生成的是离线 release 产物；正式发版前必须补齐真机验证结果。"

for doc in test-report.md core-flow-endurance.md user-manual.md; do
    doc_path="$RELEASE_DIR/$doc"
    sum="$(sha256_of "$doc_path")"
    size="$(wc -c < "$doc_path" | tr -d ' ')"
    echo "${doc}  ${size}  ${sum}" >> "$MANIFEST"
done
if [ -d "$RELEASE_DIR/images" ]; then
    while IFS= read -r img; do
        rel="${img#$RELEASE_DIR/}"
        sum="$(sha256_of "$img")"
        size="$(wc -c < "$img" | tr -d ' ')"
        echo "${rel}  ${size}  ${sum}" >> "$MANIFEST"
    done < <(find "$RELEASE_DIR/images" -type f | sort)
fi
if [ -d "$RELEASE_DIR/evidence" ]; then
    while IFS= read -r evidence_path; do
        evidence_rel="${evidence_path#$RELEASE_DIR/}"
        evidence_sum="$(sha256_of "$evidence_path")"
        evidence_size="$(wc -c < "$evidence_path" | tr -d ' ')"
        echo "${evidence_rel}  ${evidence_size}  ${evidence_sum}" >> "$MANIFEST"
    done < <(find "$RELEASE_DIR/evidence" -type f -name 'device-acceptance-*.json' | sort)
fi

# 版本号一致性校验：产物文件名必须含当前固件版本与标识信息，且版本号只出现一次
for fw in "${PRODUCED_BINS[@]}"; do
    case "$fw" in
        *"-v${VERSION}-stable-fix-g${GIT_SHA}.bin") ;;
        *) echo "错误：产物名 '$fw' 与版本元信息不一致"; exit 1 ;;
    esac
done

echo ""
echo "=== Release 打包：固件 + 报告 + 手册 ==="
if ! command -v zip >/dev/null 2>&1; then
    echo "错误：未找到 zip 命令，无法生成 release 总包"
    exit 1
fi
PACKAGE_NAME="bikepower-c-release-v${VERSION}-stable-fix-g${GIT_SHA}.zip"
PACKAGE_ITEMS=("${PRODUCED_BINS[@]}")
for fw in "${PRODUCED_BINS[@]}"; do
    PACKAGE_ITEMS+=("${fw}.sha256")
done
PACKAGE_ITEMS+=("manifest.txt" "test-report.md" "core-flow-endurance.md" "user-manual.md")
if [ -d "$RELEASE_DIR/images" ]; then
    PACKAGE_ITEMS+=("images")
fi
if [ -d "$RELEASE_DIR/evidence" ]; then
    PACKAGE_ITEMS+=("evidence")
fi

rm -f "$RELEASE_DIR/$PACKAGE_NAME" "$RELEASE_DIR/${PACKAGE_NAME}.sha256"
(cd "$RELEASE_DIR" && zip -qr "$PACKAGE_NAME" "${PACKAGE_ITEMS[@]}")
package_sum="$(sha256_of "$RELEASE_DIR/$PACKAGE_NAME")"
package_size="$(wc -c < "$RELEASE_DIR/$PACKAGE_NAME" | tr -d ' ')"
echo "${package_sum}  ${PACKAGE_NAME}" > "$RELEASE_DIR/${PACKAGE_NAME}.sha256"
package_sha_sum="$(sha256_of "$RELEASE_DIR/${PACKAGE_NAME}.sha256")"
package_sha_size="$(wc -c < "$RELEASE_DIR/${PACKAGE_NAME}.sha256" | tr -d ' ')"
echo "${PACKAGE_NAME}  ${package_size}  ${package_sum}" >> "$MANIFEST"
echo "${PACKAGE_NAME}.sha256  ${package_sha_size}  ${package_sha_sum}" >> "$MANIFEST"
echo "  总包: $RELEASE_DIR/$PACKAGE_NAME"
ls -lh "$RELEASE_DIR/$PACKAGE_NAME" | awk '{print "  大小: "$5}'

if [ "${#TARGETS[@]}" -eq 2 ]; then
    echo ""
    echo "=== Release 产物门禁：附件 / SHA256 / manifest / zip ==="
    python3 "$PROJECT_DIR/scripts/tools/check_release_artifacts.py" \
        --release-dir "$RELEASE_DIR" \
        --version "$VERSION" \
        --git-sha "$GIT_SHA" \
        --allow-dirty
else
    echo ""
    echo "=== Release 产物门禁：跳过 ==="
    echo "当前仅构建单板型，完整 10 附件发布门禁需双板型构建后执行。"
fi

echo ""
echo "=== 构建完成，产物在 $RELEASE_DIR/ ==="
ls -lh "$RELEASE_DIR/"
echo ""
echo "⚠️  发布前人工步骤（本脚本不自动执行）："
echo "   1. 校验各产物 SHA256 与 manifest.txt 一致"
echo "   2. 补齐真机验证记录，更新 test-report.md"
echo "   3. 运行 device_acceptance.py validate --strict，确认全部 P0 证据通过"
echo "   4. 推送 git tag，使用 publish_github_release.py 创建 GitHub Release 并上传 10 个标准附件"
echo "   本脚本不会打 git tag、不会创建 GitHub Release。"
echo "   永久放弃设备内 OTA：普通用户升级统一走网页端在线烧录。"
