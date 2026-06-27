#!/bin/bash
# OTA 全流程自动测试：配网门户 → 升级 → 降级保护 → 版本确认
#
# 用法：
#   tests/ota_full_test.sh --baseline <旧版本bin> --target <新版本bin> [选项]
#
# 必填：
#   --baseline <bin>   旧版本固件（先烧到设备 ota_0 作为升级起点）
#   --target  <bin>    新版本固件（OTA 上传验证升级）
#
# 可选（带默认值）：
#   --port <serial>      串口设备（默认 /dev/ttyACM0）
#   --iface <wifi_if>    本机无线网卡（默认 wlp1s0）
#   --ap-ssid <ssid>     设备 SoftAP SSID（默认 BikePower）
#   --ap-psk <psk>       设备 SoftAP 密码（默认 12345678）
#   --home-ssid <ssid>   测试结束连回的家庭 WiFi（默认 CU_juuw）
#
# 前置：
#   1. 设备已烧 baseline 到 ota_0 并从 ota_0 启动（esptool erase_flash + write_flash 0x10000 baseline 0x390000 ota_data_initial）
#   2. baseline 需带自动配网能力（OTA_TEST_AUTO_CONFIG）或手动按按钮进配网
#   3. 本机 nmcli 可用，无线网卡 $IFACE 已启用
#
# 退出码：0 全通过，非 0 某步失败（见日志末尾 FAIL 行）
set -uo pipefail
cd "$(dirname "$0")/.."

PORT=/dev/ttyACM0
IFACE=wlp1s0
AP_SSID=BikePower
AP_PSK=12345678
HOME_SSID=CU_juuw
BASELINE=""
TARGET=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --baseline) BASELINE=$2; shift 2;;
        --target)  TARGET=$2;  shift 2;;
        --port)    PORT=$2;    shift 2;;
        --iface)   IFACE=$2;   shift 2;;
        --ap-ssid) AP_SSID=$2; shift 2;;
        --ap-psk)  AP_PSK=$2;  shift 2;;
        --home-ssid) HOME_SSID=$2; shift 2;;
        *) echo "未知参数 $1"; exit 2;;
    esac
done

[[ -z "$BASELINE" ]] && { echo "需 --baseline <旧版本bin>"; exit 2; }
[[ -z "$TARGET" ]]  && { echo "需 --target <新版本bin>";  exit 2; }
[[ -f "$BASELINE" ]] || { echo "baseline 不存在: $BASELINE"; exit 2; }
[[ -f "$TARGET" ]]   || { echo "target 不存在: $TARGET";   exit 2; }

echo "===== OTA 全流程测试 $(date) ====="
echo "baseline=$(basename $BASELINE) target=$(basename $TARGET) port=$PORT iface=$IFACE ap=$AP_SSID"

connect_bp() {
    nmcli connection delete BikePowerTest 2>/dev/null
    nmcli connection delete BikePower 2>/dev/null
    nmcli connection add type wifi ifname "$IFACE" con-name BikePowerTest \
        802-11-wireless.ssid "$AP_SSID" \
        802-11-wireless-security.key-mgmt wpa-psk \
        802-11-wireless-security.psk "$AP_PSK" 2>&1
    nmcli connection up BikePowerTest ifname "$IFACE" 2>&1
}

trap 'nmcli device wifi connect "$HOME_SSID" 2>/dev/null' EXIT

# 0. 重启设备
echo "--- 重启设备 ---"
python3 -c "import serial,time; s=serial.Serial('$PORT',115200); s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False); time.sleep(0.1); s.setDTR(True); s.close()" 2>/dev/null
sleep 6

# 1. 连设备 SoftAP
echo "--- 连接 $AP_SSID AP ---"
ok=0
for i in 1 2 3; do
    connect_bp && { ok=1; break; }
    echo "重试 $i"; sleep 2
done
[[ $ok -eq 1 ]] || { echo "FAIL: nmcli 连接失败"; exit 1; }
ping -c 2 -W 2 192.168.4.1 || { echo "FAIL: ping 不通"; exit 1; }

# 2. 配网门户基本路由
echo "--- 配网门户路由测试 ---"
python3 tests/stage5_portal_test.py
echo "portal退出=$?"

# 3. /ota 页面
echo "--- /ota 页面测试 ---"
python3 -c "
import urllib.request
try:
    r=urllib.request.urlopen('http://192.168.4.1/ota',timeout=10)
    b=r.read().decode('utf-8','replace')
    print('status=',r.status,'len=',len(b),'含固件升级=', '固件升级' in b, '含file=', 'type=\"file\"' in b)
except Exception as e:
    print('FAIL',e)
"

# 4. OTA 上传 target（期望成功，升级）
echo "--- OTA 上传 target（期望成功）---"
python3 tests/ota_upload_test.py --file "$TARGET" --expect success
echo "upload_target退出=$?"

# 5. 等设备重启 + 重新进配网
echo "--- 等待设备重启（18s）---"
sleep 18

# 6. 重连 SoftAP
echo "--- 重连 $AP_SSID AP ---"
ok=0
for i in 1 2 3 4 5; do
    connect_bp && { ok=1; break; }
    echo "重试 $i"; sleep 2
done
[[ $ok -eq 1 ]] || { echo "FAIL: 重启后连接失败"; exit 1; }
ping -c 3 -W 2 192.168.4.1 || { echo "FAIL: 重启后 ping 不通"; exit 1; }

# 7. 降级测试：上传 baseline（期望拒绝）
echo "--- 降级测试：上传 baseline（期望失败）---"
python3 tests/ota_upload_test.py --file "$BASELINE" --expect fail
echo "upload_baseline降级退出=$?"

# 8. 抓串口确认版本（应为 target 版本）+ 自检
echo "--- 抓串口日志（确认版本 + 自检）---"
python3 -c "
import serial,time
s=serial.Serial('$PORT',115200,timeout=1)
s.setDTR(False);s.setRTS(True);time.sleep(0.1);s.setRTS(False);time.sleep(0.1);s.setDTR(True)
time.sleep(0.3)
end=time.time()+6
buf=b''
while time.time()<end:
    d=s.read(4096)
    if d:buf+=d
s.close()
for line in buf.decode('utf-8','replace').split('\n'):
    if any(k in line for k in ['version','启动','OTA','自检','PENDING','BLE 就绪']):
        print(line)
"

# 9. 连回家庭 WiFi
echo "--- 连回 $HOME_SSID ---"
nmcli device wifi connect "$HOME_SSID" 2>/dev/null
echo "$HOME_SSID退出=$?"
echo "===== 测试完成 $(date) ====="
