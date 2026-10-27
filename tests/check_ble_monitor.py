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
        ('switchMode("connect")', "默认进入实时监控主任务"),
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
