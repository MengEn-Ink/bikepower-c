#!/usr/bin/env python3
"""BLE 浏览器监控客户端静态门禁。

检查本地 Web Bluetooth 监控页面和本地服务入口是否存在，并覆盖自动重连、
骑行/跑步数据解析、异常检测和导出能力。
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
HTML = ROOT / "tools" / "web" / "ble-monitor.html"
SERVER = ROOT / "scripts" / "tools" / "serve_ble_monitor.py"
OP_DOC = ROOT / "docs" / "dev" / "05-operation-test-gate.md"
TESTS_README = ROOT / "tests" / "README.md"
QUALITY_GATE = ROOT / "scripts" / "tools" / "quality_gate.py"
OP_GATE = ROOT / "tests" / "operation_gate.py"
SITE_BUILDER = ROOT / "scripts" / "tools" / "prepare_web_flash_site.py"
SITE_BUILDER_TEST = ROOT / "tests" / "test_prepare_web_flash_site.py"
PAGES_WORKFLOW = ROOT / ".github" / "workflows" / "deploy-web-tools.yml"

TAG = "[ble-monitor-check]"


def check(condition, message, failures):
    if condition:
        print(f"{TAG} PASS {message}")
    else:
        failures.append(message)
        print(f"{TAG} FAIL {message}")


def main():
    failures = []
    check(HTML.is_file(), "存在 tools/web/ble-monitor.html", failures)
    check(SERVER.is_file(), "存在 scripts/tools/serve_ble_monitor.py", failures)
    check(SITE_BUILDER.is_file(), "存在网页刷写站点生成器", failures)
    check(SITE_BUILDER_TEST.is_file(), "存在网页刷写站点离线回归", failures)
    check(PAGES_WORKFLOW.is_file(), "存在 GitHub Pages 部署工作流", failures)
    if failures:
        print(f"{TAG} 结果: {len(failures)} 失败")
        return 1

    html = HTML.read_text(encoding="utf-8")
    server = SERVER.read_text(encoding="utf-8")
    op_doc = OP_DOC.read_text(encoding="utf-8")
    tests_readme = TESTS_README.read_text(encoding="utf-8")
    quality_gate = QUALITY_GATE.read_text(encoding="utf-8")
    op_gate = OP_GATE.read_text(encoding="utf-8")
    site_builder = SITE_BUILDER.read_text(encoding="utf-8")
    pages_workflow = PAGES_WORKFLOW.read_text(encoding="utf-8")

    required_html_tokens = [
        ("navigator.bluetooth.requestDevice", "支持首次授权选择 BikePower 设备"),
        ("filters:CYCLING_SERVICE_FILTERS", "首次设备选择仅显示广播骑行相关服务的设备"),
        ("CYCLING_SERVICE_SET.has", "连接后复核设备实际提供骑行相关 GATT 服务"),
        ("navigator.bluetooth.getDevices", "支持已授权设备自动重连"),
        ("watchAdvertisements", "刷新后等待已授权设备重新广播再自动连接"),
        ("LAST_DEVICE_KEY", "记住上次连接的设备并优先自动连接"),
        ("BroadcastChannel", "多标签页通过 BroadcastChannel 关联同一设备"),
        ("initAutoLink();", "页面加载后启动自动连接与跨标签页关联"),
        ("id=\"autolink-enabled\"", "提供自动连接开关"),
        ("7a5a0008-4c7a-4f2a-9b3e-6d1f4a8c0b21", "声明设备身份覆盖特征 UUID"),
        ("openTemplateWriter", "模板可写入开发板身份信息"),
        ("performIdentityWrite", "身份写入后回读确认 NVS 落盘"),
        ("saveAsTemplate(rec,true)", "比对页复制为模板后直接进入写入流程"),
        ("function classifyDeviceRole", "按 GATT 私有服务识别开发板角色，不依赖设备名"),
        ("BikePower 开发板 · 旧固件", "识别缺少身份覆盖特征的旧固件开发板"),
        ('id="compare-flow"', "比对页提供三步写入流程指示"),
        ("addCurrentToCompare", "比对页可加入实时监控中已连接的设备"),
        ('id="tool-launchpad"', "手册首页提供工具导航入口"),
        ('id="ch16"', "手册新增设备比对与模板写入章节"),
        ("applyRoute();", "启动时按地址路由，默认进入使用手册"),
        ("routeFromHash", "工具页支持 #/monitor 等独立链接"),
        ('id="connect-gate"', "进站先弹出蓝牙连接门（类钱包连接）"),
        ("initLinkUi();", "启动时决定是否弹出蓝牙连接门"),
        ('id="link-capsule"', "右上角常驻蓝牙连接状态胶囊"),
        ("gateSuccess", "连接成功后关闭连接门并默认打开使用手册"),
        ('id="manual-linkbar"', "手册首页按设备角色推荐下一步"),
        ('id="serial-scope"', "刷写页声明 USB 串口仅用于刷写"),
        ("GATE_SKIP_KEY", "可选择稍后连接且本会话不再打扰"),
        ("decodeGattValue", "GATT 特征值解析为中文可读数值"),
        ("GATT_CHAR_CN", "GATT 特征码附中文名称与说明"),
        ("setInterval(checkAnomalies", "周期检测通知中断等异常"),
        ("ANOMALY_LIMITS", "集中定义异常阈值"),
        ("parseCyclingPower", "解析 CPS 功率通知"),
        ("parseHeartRate", "解析 HRS 心率通知"),
        ("parseCsc", "解析 CSC 踏频通知"),
        ("function parseCsc(rec,view)", "CSC 解析显式接收设备上下文"),
        ("const c=parseCsc(rec,view)", "CSC 通知传入设备上下文，避免运行时引用错误"),
        ("parseIndoorBike", "解析 FTMS Indoor Bike 通知"),
        ("result.power", "FTMS 解析按当前固件变长数据提取功率"),
        ("result.cadence", "FTMS 解析可同步踏频字段"),
        ("flags & 0x0040", "FTMS 仅在 Instantaneous Power Present 时解析功率"),
        ("parseRsc", "解析 RSC 跑步通知"),
        ("distanceDm / 10", "RSC 累计距离按 0.1m 单位换算"),
        ("recordAnomaly", "异常统一计数和日志"),
        ("downloadReport", "支持导出监控报告"),
        ("switchMode('connect')", "导航提供实时监控主任务入口"),
        ('id="trace-power"', "实时功率轨迹由设备通知驱动"),
        ("HISTORY_RETENTION_MS", "曲线历史按会话长期保留而非固定 30 秒截断"),
        ("HISTORY_MAX_SAMPLES", "曲线历史设置浏览器内存上限"),
        ("TRACE_RANGE_OPTIONS", "支持 30 秒、2 分钟、10 分钟与全程时间范围"),
        ("renderTraceXAxis", "X 轴随会话时长动态生成刻度"),
        ("inspectTraceAtPointer", "支持指针查看曲线具体时间点数值"),
        ("handleTraceKey", "支持键盘逐点检查曲线数值"),
        ("toggleTracePause", "支持暂停和继续图表跟随"),
        ("toggleTraceSeries", "支持独立显示或隐藏功率、踏频和心率"),
        ("summarizeSeries", "显示可见区间最小值、平均值和最大值"),
        ("downsampleTracePoints", "长会话曲线绘制前执行保峰降采样"),
        ("downloadSignalCsv", "支持导出完整会话 CSV"),
        ("7a5a0000-4c7a-4f2a-9b3e-6d1f4a8c0b21", "声明 BikePower 配置服务 UUID"),
        ("CONFIG_GROUPS", "配置按 Core/Profile/Running/LED/Identity 五组读写"),
        ("readDeviceConfig", "连接后读取开发板全部配置"),
        ("saveDeviceConfig", "支持编辑并保存开发板配置"),
        ("makeConfigBegin", "保存前建立带 revision 的配置事务"),
        ("makeConfigCommit", "完整写入配置组后提交事务"),
        ("waitForConfigResponse", "等待设备端事务与 NVS 保存结果"),
        ("configsEqual", "使用协议编码比较配置脏状态"),
        ('id="config-error-summary"', "配置校验提供可聚焦错误摘要"),
        ("保存到设备", "配置编辑器提供显式保存操作"),
        ("全部配置已写入开发板 NVS", "仅在设备确认 NVS 成功后反馈保存成功"),
        ("重新连接后才能读取或保存", "断连后禁用设备配置操作并提示重连"),
        ("服务结构已变化，页面将断开连接", "Profile 或广播变化后提示 GATT 重建断连"),
        ("已断开 · 历史保留", "设备断开后保留会话曲线和统计"),
        ("notificationGapText", "展示各服务的真实通知新鲜度"),
        ("escapeHtml", "转义蓝牙设备与 GATT 动态字段"),
        ("00001818-0000-1000-8000-00805f9b34fb", "包含 Cycling Power Service UUID"),
        ("00001814-0000-1000-8000-00805f9b34fb", "包含 Running Speed and Cadence Service UUID"),
        ("00001826-0000-1000-8000-00805f9b34fb", "包含 Fitness Machine Service UUID"),
        ("0000180d-0000-1000-8000-00805f9b34fb", "包含 Heart Rate Service UUID"),
        ("0000180a-0000-1000-8000-00805f9b34fb", "包含 Device Information Service UUID"),
        ("esp-web-install-button", "集成 ESP Web Tools 刷写组件"),
        ("esp-web-tools@10", "固定 ESP Web Tools 主版本"),
        ("installEspWebToolsTheme", "ESP Web Tools 动态弹窗接入 BikePower 主题兼容层"),
        ("bikepower-flash-helper", "安装进度弹窗提供中文小白说明"),
        ("--md-sys-color-surface", "安装进度弹窗使用 BikePower 暗色表面令牌"),
        ('customElements.get("esp-web-install-button")', "刷写组件加载失败时不会保留误导按钮"),
        ("./firmware/latest.json", "自动读取部署生成的最新固件索引"),
        ('value="generic-c3"', "支持通用 ESP32-C3 固件"),
        ("renderSelectedFirmware", "板型切换会更新刷写 manifest"),
        ("firmware-sha256", "页面展示固件 SHA256"),
        ("firmware-download", "页面提供同源固件下载"),
        ("Web Serial", "页面说明 Web Serial 安全限制"),
        ("HTTPS", "页面说明网页刷写的安全上下文要求"),
        ("./firmware/releases.json", "读取部署镜像的历史固件版本索引"),
        ('id="fw-timeline"', "刷写页提供固件版本时间轴"),
        ("selectFirmwareRelease", "支持切换到历史版本回滚"),
        ('id="fw-rollback-ack"', "回滚历史版本前必须显式确认"),
        ("flash-anim", "刷写页提供刷写过程动画示意"),
        ("web-flash-serial-port.jpg", "刷写页附串口选择真实截图"),
        ('id="ide-dock"', "提供 IDE 式底部常驻面板"),
        ('data-pane="gatt"', "GATT 服务列表作为常驻面板"),
        ('data-pane="nvs"', "开发板 NVS 配置作为常驻面板"),
        ('data-pane="templates"', "模板库合并为常驻面板"),
        ('data-pane="log"', "事件日志作为常驻面板"),
        ('id="ide-status"', "提供 IDE 式底部状态栏"),
        ("toggleIdePane", "侧栏可展开 / 隐藏"),
        ("IDE_KEY", "窗口布局记忆在本浏览器"),
    ]
    for token, message in required_html_tokens:
        check(token in html, message, failures)
    check(not re.search(r"acceptAllDevices\s*:\s*true", html),
          "设备选择器不再展示所有无关蓝牙设备", failures)
    check("now-sample.t<=30000" not in html,
          "曲线不再把历史硬裁剪为最近 30 秒", failures)
    cycling_filter = re.search(
        r"const CYCLING_SERVICE_UUIDS\s*=\s*\[(.*?)\];", html, flags=re.S
    )
    cycling_filter_src = cycling_filter.group(1) if cycling_filter else ""
    for service_name in ("cpsService", "hrService", "cscService", "ftmsService"):
        check(service_name in cycling_filter_src,
              f"设备选择器包含 {service_name} 骑行服务过滤条件", failures)
    check("rscService" not in cycling_filter_src,
          "仅 RSC 跑步设备不会进入骑行设备选择列表", failures)

    check("ThreadingHTTPServer" in server and "tools/web" in server,
          "本地服务脚本从 tools/web 提供页面", failures)
    check("Cache-Control" in server and "no-store" in server,
          "本地服务禁止缓存旧监控页面", failures)
    check("ble-monitor.html" in server and "127.0.0.1" in server,
          "本地服务默认指向 localhost 页面", failures)

    scripts = re.findall(r"<script>(.*?)</script>", html, flags=re.S)
    check(len(scripts) == 1, "监控页包含单个内联脚本，便于静态语法检查", failures)
    for legacy_id in ('id="templates-card"', 'id="compare-templates-card"', 'id="log-panel"', 'id="connect-result"'):
        check(legacy_id not in html, f"旧的页面底部长卡片已并入常驻面板（{legacy_id}）", failures)
    node = shutil.which("node")
    if node and scripts:
        with tempfile.NamedTemporaryFile("w", suffix=".js", encoding="utf-8", delete=False) as tmp:
            tmp.write(scripts[0])
            tmp_path = tmp.name
        try:
            result = subprocess.run([node, "--check", tmp_path], capture_output=True, text=True, timeout=15)
            check(result.returncode == 0, "监控页内联 JS 语法正确", failures)
            if result.returncode != 0:
                print((result.stderr or result.stdout).strip())
        finally:
            Path(tmp_path).unlink(missing_ok=True)

        runtime_prelude = """
const window = { addEventListener() {} };
const document = {};
const navigator = {};
const customElements = {};
class MutationObserver {}
"""
        runtime_assertions = """
const fixture = {
  schema_ver: 12, device_name: "BikePower-C3", sport_profile: 0, mode: 1,
  power: 260, cadence: 85, has_user_hr: 0, heartrate: 145, power_step: 20,
  power_float: 50, cadence_float: 8, hr_float: 7, broadcast_mask: 7,
  weight: 70, ftp: 250, thr_hr: 175, ride_type: 2,
  custom_wkg_low: 20, custom_wkg_high: 28, custom_pacer_name: "Custom Ride",
  run_speed_x100: 1000, run_pace_float_sec: 12, run_cadence: 170,
  run_cadence_float: 5, run_mode: 1, run_pacer_type: 1,
  run_pace_low_sec: 300, run_pace_high_sec: 420, run_custom_name: "Custom Run",
  led_silent: 0, led_scenes: [
    {mode: 2, color: 2}, {mode: 1, color: 1}, {mode: 3, color: 3},
    {mode: 3, color: 3}, {mode: 4, color: 0},
  ],
};
const merged = {schema_ver: fixture.schema_ver};
const lengths = [];
for (const group of CONFIG_GROUPS) {
  const bytes = encodeConfigGroup(group.key, fixture);
  lengths.push(bytes.length);
  Object.assign(merged, parseConfigGroup(
    group.key, new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  ));
}
const expectedCore = [1, 4, 1, 85, 0, 1, 145, 0, 0, 50, 8, 7, 20, 0, 7];
const actualCore = Array.from(encodeConfigGroup("core", fixture));
const begin = Array.from(makeConfigBegin(0x01020304, 0x11223344));
const commit = Array.from(makeConfigCommit(0x01020304));
if (!configsEqual(fixture, merged)
    || JSON.stringify(lengths) !== JSON.stringify([15, 22, 24, 12, 14])
    || JSON.stringify(actualCore) !== JSON.stringify(expectedCore)
    || JSON.stringify(begin) !== JSON.stringify([1, 1, 4, 3, 2, 1, 68, 51, 34, 17])
    || JSON.stringify(commit) !== JSON.stringify([2, 4, 3, 2, 1, 31])) {
  throw new Error("BLE 配置网页协议运行时回归失败");
}
const dv = arr => { const u = new Uint8Array(arr); return new DataView(u.buffer); };
const identity = {manufacturer: "萌恩", model: "BP-C3", serial: "", hardware: "v2", firmware: "1.4.0", software: ""};
const idBytes = encodeIdentityProfile(identity);
const idBack = decodeIdentityProfile(new DataView(idBytes.buffer, idBytes.byteOffset, idBytes.byteLength));
if (idBytes[0] !== 1 || idBytes[1] !== 6 || JSON.stringify(idBack) !== JSON.stringify(identity)) {
  throw new Error("设备身份覆盖编解码往返失败");
}
let rejected = false;
try { encodeIdentityProfile({manufacturer: "x".repeat(32)}); } catch (e) { rejected = true; }
if (!rejected) throw new Error("身份覆盖超长字段未被拒绝");
rejected = false;
try { decodeIdentityProfile(dv([2, 1, 0])); } catch (e) { rejected = true; }
if (!rejected) throw new Error("身份覆盖错误版本未被拒绝");
const cps = decodeGattValue("00002a63-0000-1000-8000-00805f9b34fb", dv([0, 0, 0x04, 0x01]));
const hr = decodeGattValue("00002a37-0000-1000-8000-00805f9b34fb", dv([0, 145]));
const bike = decodeGattValue("00002ad2-0000-1000-8000-00805f9b34fb", dv([0x44, 0, 0xe8, 0x03, 0xaa, 0, 0x04, 0x01]));
const status = decodeGattValue("00002ada-0000-1000-8000-00805f9b34fb", dv([0x08, 0xc8, 0]));
const range = decodeGattValue("00002ad8-0000-1000-8000-00805f9b34fb", dv([0, 0, 0xe8, 0x03, 5, 0]));
const name = decodeGattValue("00002a29-0000-1000-8000-00805f9b34fb", dv([0x42, 0x50]));
if (!cps.includes("功率：260 W") || !hr.includes("心率：145 bpm")
    || !bike.includes("功率：260 W") || !bike.includes("踏频：85.0 rpm")
    || !status.includes("目标功率已变更") || !status.includes("200 W")
    || !range.includes("0–1000 W") || !name.includes("BP")) {
  throw new Error("GATT 中文数值解析回归失败: " + [cps, hr, bike, status, range, name].join(" | "));
}
if (gattCharInfo("00002a63-0000-1000-8000-00805f9b34fb").cn !== "骑行功率测量"
    || gattServiceInfo("00001826-0000-1000-8000-00805f9b34fb").cn.indexOf("健身") < 0) {
  throw new Error("GATT 中文描述字典缺失");
}
const svc = (uuid, chars) => ({uuid, characteristics: (chars || []).map(c => ({uuid: c}))});
const boardRec = {info: {manufacturer: "BikePower"}, services: [svc(UUIDS.cpsService), svc(UUIDS.configService, [UUIDS.configMeta, IDENTITY_PROFILE_UUID])]};
const legacyRec = {info: {}, services: [svc(UUIDS.cpsService), svc(UUIDS.configService, [UUIDS.configMeta])]};
const extRec = {info: {manufacturer: "Garmin", model: "Rally", firmware: "3.1"}, services: [svc(UUIDS.cpsService), svc(UUIDS.disService)]};
// 设备名被改成任何值都不影响识别
const renamedExt = Object.assign({device: {name: "BikePower"}}, extRec);
const rb = classifyDeviceRole(boardRec), rl = classifyDeviceRole(legacyRec), re = classifyDeviceRole(renamedExt);
if (rb.role !== "board" || !rb.writable || rl.role !== "board-legacy" || rl.writable
    || re.role !== "external" || re.writable || re.disCount !== 3
    || !supportsIdentityProfile(boardRec) || supportsIdentityProfile(legacyRec) || supportsIdentityProfile(renamedExt)) {
  throw new Error("开发板角色识别回归失败: " + [rb.role, rl.role, re.role, re.disCount].join(","));
}
const routes = [["", "manual"], ["#/monitor", "connect"], ["#/compare", "compare"], ["#flash", "flash"], ["#/erg", "erg"], ["#/unknown", "manual"]];
for (const [hash, mode] of routes) {
  if (routeFromHash(hash).mode !== mode) throw new Error("路由解析失败: " + hash);
}
const chRoute = routeFromHash("#ch16");
if (chRoute.mode !== "manual" || chRoute.chapter !== "ch16") throw new Error("手册章节路由解析失败");
if (Object.keys(MANUAL_META).length !== 16) throw new Error("手册目录应为 16 节");
if (linkSnapshot().state !== "unsupported") throw new Error("无 Web Bluetooth 时胶囊应显示不支持");
navigator.bluetooth = {};
if (linkSnapshot().state !== "idle" || linkSnapshot().name !== "连接蓝牙设备") throw new Error("未连接时胶囊应提示连接蓝牙设备");
if (ROLE_SHORT[classifyDeviceRole(boardRec).role] !== "开发板 · 可写入") throw new Error("胶囊角色简称缺失");
const fwBoards = {"generic-c3": {manifest: "firmware/manifest-generic-c3.json", download: "firmware/a.bin"}};
const fwIndex = {schema_version: 1, releases: [
  {tag: "v4.2.0", latest: true, boards: fwBoards},
  {tag: "v4.1.3", latest: false, boards: {}},
  {tag: "v4.1.1", latest: false, boards: {"generic-c3": {manifest: "firmware/v4.1.1/manifest-generic-c3.json", download: "firmware/v4.1.1/a.bin"}}},
]};
const fwList = normalizeFirmwareReleases(fwIndex, null);
if (fwList.length !== 2 || fwList[0].tag !== "v4.2.0" || fwList[1].tag !== "v4.1.1") throw new Error("历史版本索引应丢弃无板型的版本");
const fwFallback = normalizeFirmwareReleases(null, {available: true, tag: "v4.2.0", version: "4.2.0", boards: fwBoards});
if (fwFallback.length !== 1 || !fwFallback[0].latest) throw new Error("缺少 releases.json 时应退回 latest.json 单版本");
if (normalizeFirmwareReleases({schema_version: 2, releases: fwIndex.releases}, null).length !== 0) throw new Error("未知索引版本不应被信任");
if (safeSitePath("firmware/v4.1.1/manifest-generic-c3.json") !== "./firmware/v4.1.1/manifest-generic-c3.json") throw new Error("历史版本 manifest 路径应被允许");
if (safeSitePath("firmware/../x") !== null) throw new Error("路径穿越必须被拒绝");
"""
        with tempfile.NamedTemporaryFile("w", suffix=".js", encoding="utf-8", delete=False) as tmp:
            tmp.write(runtime_prelude)
            tmp.write(scripts[0])
            tmp.write(runtime_assertions)
            runtime_path = tmp.name
        try:
            result = subprocess.run([node, runtime_path], capture_output=True, text=True, timeout=15)
            check(result.returncode == 0, "网页配置协议小端编码与五组往返正确", failures)
            if result.returncode != 0:
                print((result.stderr or result.stdout).strip())
        finally:
            Path(runtime_path).unlink(missing_ok=True)
    else:
        check(False, "未找到 node，无法检查监控页内联 JS 语法", failures)

    check("tests/check_ble_monitor.py" in quality_gate,
          "quality_gate.py 聚合 BLE 监控静态门禁", failures)
    check("tests/check_ble_monitor.py" in op_gate,
          "operation_gate.py 聚合 BLE 监控静态门禁", failures)
    check("tests/test_prepare_web_flash_site.py" in quality_gate,
          "quality_gate.py 聚合刷写站点生成回归", failures)
    check("tests/test_prepare_web_flash_site.py" in op_gate,
          "operation_gate.py 聚合刷写站点生成回归", failures)
    check("SHA256" in site_builder and '"ESP32-C3"' in site_builder and '"offset": 0' in site_builder,
          "站点生成器校验摘要并生成 ESP32-C3 0x0 manifest", failures)
    check("ReleaseUnavailable" in site_builder and '"available": False' in site_builder,
          "没有 Release 时站点生成器降级为禁用刷写", failures)
    for action in (
        "actions/checkout@v7",
        "actions/setup-python@v7",
        "actions/configure-pages@v6",
        "actions/upload-pages-artifact@v5",
        "actions/deploy-pages@v5",
    ):
        check(action in pages_workflow, f"Pages 工作流使用 {action}", failures)
    check("pages: write" in pages_workflow and "id-token: write" in pages_workflow,
          "Pages 工作流声明部署所需最小写权限", failures)
    check("OP-BLE-003" in op_doc and "ble-monitor.html" in op_doc,
          "操作覆盖矩阵记录 BLE 监控客户端门禁", failures)
    check("OP-WEB-004" in op_doc and "test_prepare_web_flash_site.py" in op_doc,
          "操作覆盖矩阵记录网页刷写部署门禁", failures)
    check("serve_ble_monitor.py" in tests_readme and "ble-monitor.html" in tests_readme,
          "tests/README.md 记录 BLE 监控客户端用法", failures)

    if failures:
        print(f"{TAG} 结果: {len(failures)} 失败")
        return 1
    print(f"{TAG} PASS: BLE 监控客户端门禁通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
