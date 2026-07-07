#!/usr/bin/env python3
"""TC-3d WiFi 门户静态结构检查（无需设备，v3.2.0 重构版）

v3.2.0 起 HTML 不再嵌入 C 字符串，前端三件套独立存放：
  - main/web/index.html  - SPA 入口（含占位符）
  - main/web/app.css     - 样式
  - main/web/app.js      - SPA 逻辑

静态校验四类内容，防止烧录后才发现路由缺失或字段漂移：

1. HTML 基础结构：index.html 必须含 <html>/<head>/<body>/<meta viewport>，
   且必须保留 <!-- INLINE_CSS --> 与 <!-- INLINE_JS --> 占位符（编译时替换）。
2. 路由表一致性：wifi_portal.c 注册的路由必须包含前端调用的全部 /api/xxx。
3. 前端 API 调用完整性：app.js 必须调用 /api/state（初始化）和 /api/time（轮询）。
4. 前端信息架构：底部只保留首页/设置两 Tab，用户资料、设备信息、关于并入设置页。
   物理按钮和 LED 自定义配置归入设备信息分区。

设计：仅做静态文本匹配，不解析 DOM，不依赖 bs4 / lxml，便于 CI 裸跑。

用法:
    python3 tests/check_portal_structure.py
退出码: 0 全绿, 1 有失败
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INDEX_HTML = ROOT / "main" / "web" / "index.html"
APP_JS = ROOT / "main" / "web" / "app.js"
APP_CSS = ROOT / "main" / "web" / "app.css"
PORTAL_C = ROOT / "main" / "wifi_portal.c"
BLE_SCANNER_C = ROOT / "main" / "ble_scanner.c"
CAPTIVE_PORTAL_C = ROOT / "main" / "captive_portal.c"
PORTAL_CONFIG_SAVE_TEST = ROOT / "tests" / "test_portal_config_save.py"
CORE_FLOW_ENDURANCE_TEST = ROOT / "tests" / "test_core_flow_endurance.py"
TEST_RUNNER = ROOT / "tests" / "test_runner.sh"

TAG = "[TC-3d]"
g_pass = 0
g_fail = 0
failures = []


def check(cond: bool, msg: str):
    global g_pass, g_fail
    if cond:
        g_pass += 1
    else:
        g_fail += 1
        failures.append(msg)
        print(f"{TAG}   FAIL {msg}")


REQUIRED_HTML_TAGS = [
    "<html",
    "<head",
    "<body",
    'name="viewport"',  # 移动端适配
]

REQUIRED_PLACEHOLDERS = [
    "<!-- INLINE_CSS -->",
    "<!-- INLINE_JS -->",
]

# 前端 app.js 必须调用的核心 API（防止删错路由后前端黑屏）
# 注：/api/mem_stats 自 v3.2.1 起不再被前端调用（关于页移除内存统计展示）
REQUIRED_FRONTEND_APIS = [
    "/api/state",
    "/api/time",
    "/api/renew",
    "/api/config",
    "/api/close-wifi",
    "/api/device-templates",
    "/api/device-templates/save",
    "/api/device-templates/delete",
    "/api/device-templates/apply",
    "/api/ble-scan/start",
    "/api/ble-scan/stop",
    "/api/ble-scan/results",
    "/api/ble-scan/read",
]

# wifi_portal.c 必须注册的核心路由（防止改坏路由表）
REQUIRED_BACKEND_ROUTES = REQUIRED_FRONTEND_APIS + ["/"]


def main():
    if not INDEX_HTML.is_file():
        print(f"{TAG} FAIL: 文件不存在 {INDEX_HTML}", file=sys.stderr)
        return 1
    if not APP_JS.is_file():
        print(f"{TAG} FAIL: 文件不存在 {APP_JS}", file=sys.stderr)
        return 1
    if not APP_CSS.is_file():
        print(f"{TAG} FAIL: 文件不存在 {APP_CSS}", file=sys.stderr)
        return 1
    if not PORTAL_C.is_file():
        print(f"{TAG} FAIL: 文件不存在 {PORTAL_C}", file=sys.stderr)
        return 1
    if not BLE_SCANNER_C.is_file():
        print(f"{TAG} FAIL: 文件不存在 {BLE_SCANNER_C}", file=sys.stderr)
        return 1
    if not CAPTIVE_PORTAL_C.is_file():
        print(f"{TAG} FAIL: 文件不存在 {CAPTIVE_PORTAL_C}", file=sys.stderr)
        return 1
    if not PORTAL_CONFIG_SAVE_TEST.is_file():
        print(f"{TAG} FAIL: 文件不存在 {PORTAL_CONFIG_SAVE_TEST}", file=sys.stderr)
        return 1
    if not CORE_FLOW_ENDURANCE_TEST.is_file():
        print(f"{TAG} FAIL: 文件不存在 {CORE_FLOW_ENDURANCE_TEST}", file=sys.stderr)
        return 1
    if not TEST_RUNNER.is_file():
        print(f"{TAG} FAIL: 文件不存在 {TEST_RUNNER}", file=sys.stderr)
        return 1

    html = INDEX_HTML.read_text(encoding="utf-8")
    js = APP_JS.read_text(encoding="utf-8")
    css = APP_CSS.read_text(encoding="utf-8")
    csrc = PORTAL_C.read_text(encoding="utf-8")
    scanner_src = BLE_SCANNER_C.read_text(encoding="utf-8")
    captive_src = CAPTIVE_PORTAL_C.read_text(encoding="utf-8")
    portal_config_save_src = PORTAL_CONFIG_SAVE_TEST.read_text(encoding="utf-8")
    core_flow_endurance_src = CORE_FLOW_ENDURANCE_TEST.read_text(encoding="utf-8")
    test_runner_src = TEST_RUNNER.read_text(encoding="utf-8")

    print(f"{TAG} 检查 index.html: {len(html)} 字节")
    print(f"{TAG} 检查 app.js: {len(js)} 字节")
    print(f"{TAG} 检查 app.css: {len(css)} 字节")
    print(f"{TAG} 检查 wifi_portal.c: {len(csrc)} 字节")
    print(f"{TAG} 检查 ble_scanner.c: {len(scanner_src)} 字节")
    print(f"{TAG} 检查 captive_portal.c: {len(captive_src)} 字节")
    print(f"{TAG} 检查 test_portal_config_save.py: {len(portal_config_save_src)} 字节")
    print(f"{TAG} 检查 test_core_flow_endurance.py: {len(core_flow_endurance_src)} 字节")

    # 1. HTML 基础结构
    print(f"\n{TAG} [1/4] HTML 基础结构")
    for tag in REQUIRED_HTML_TAGS:
        check(tag in html, f"HTML 标记 {tag!r} 缺失")
    for ph in REQUIRED_PLACEHOLDERS:
        check(ph in html, f"占位符 {ph!r} 缺失")

    # 2. 后端路由表完整性
    print(f"{TAG} [2/4] wifi_portal.c 路由表完整性")
    for route in REQUIRED_BACKEND_ROUTES:
        # 路由在 C 源码中以 ".uri = "/api/xxx"" 形式出现
        pat = '.uri = "' + route + '"'
        check(pat in csrc, f"路由 {route} 未在 wifi_portal.c 注册")

    # 3. 前端 API 调用完整性
    print(f"{TAG} [3/4] app.js 前端 API 调用完整性")
    for api in REQUIRED_FRONTEND_APIS:
        # app.js 中可能以 "/api/xxx" 字符串调用，或拼接，统一查字符串
        check(('"' + api + '"') in js, f"app.js 未调用 {api}")

    # 4. 前端信息架构
    print(f"{TAG} [4/4] 前端信息架构")
    nav_items = re.findall(r'<a\b[^>]*class="nav-item"[^>]*data-h="([^"]+)"', html)
    check(nav_items == ["/home", "/settings"], "底部导航必须只有 首页/设置 两个 Tab")
    check('id="view-settings"' in html, "缺少设置视图 view-settings")
    check('id="view-more"' not in html, "旧更多视图 view-more 必须并入设置")
    check('id="view-about"' not in html, "旧关于视图 view-about 必须并入设置")
    check('id="settings-profile"' in html, "设置页缺少用户资料分区")
    check('id="settings-device"' in html, "设置页缺少设备信息分区")
    check('id="settings-about"' in html, "设置页缺少关于分区")
    check('id="settings-device-name"' in html, "设备名称输入必须挪入设置页")
    check('id="device-name-display"' in html, "设备名称必须默认以文本形式展示")
    check('id="device-name-edit"' in html, "设备名称必须提供右侧编辑按钮")
    check('id="device-name-confirm"' in html, "设备名称编辑必须提供对勾确认按钮")
    check('onDeviceNameEdit' in js and 'onDeviceNameConfirm' in js, "app.js 缺少设备名称编辑/确认交互")
    check('id="settings-menu"' in html, "设备信息页必须提供设置菜单入口列表")
    check('设备模板' in html and '物理按钮' in html and 'LED 指示' in html, "设置菜单入口名称必须短且对齐")
    check('id="settings-templates"' in html, "模板库必须迁移到设置子页面")
    check('id="settings-button"' in html, "物理按钮必须迁移到设置子页面")
    check('id="settings-led"' in html, "LED 必须迁移到设置子页面")
    check('id="template-list"' in html, "设备信息模板库缺少列表容器")
    check('id="template-scan-btn"' in html, "设备信息模板库缺少扫描入口按钮")
    check('loadTemplates' in js and 'renderTemplates' in js, "app.js 缺少模板加载/渲染逻辑")
    check('firmware_name' in csrc and 'board_firmware_name' in csrc, "wifi_portal.c 必须输出包含板型/Git/稳定版/修复版的 firmware_name")
    check('firmware_name' in js and 'firmwareLabel' in js and 'firmwareShortLabel' in js, "app.js 必须保留完整固件名并提供短标签展示")
    check('version-short' in css and 'version-full' in css, "app.css 必须同时支持固件短标签和完整版本详情")
    check('"/settings/templates"' in js and '"/settings/button"' in js and '"/settings/led"' in js, "app.js 必须注册设置子路由")
    check('settingsSubpageBack' in js and 'settings-menu-row' in css, "设置子页面必须有返回与菜单行样式")
    check('pollScanResults' in js and 'onTemplateReadDevice' in js, "app.js 缺少 BLE 扫描轮询/读取逻辑")
    check('filterNamedDevices' in js and 'hasDeviceName' in js, "app.js 必须过滤未命名 BLE 扫描设备")
    check('filterUniqueDevices' in js and 'templateIdentityKey' in js, "app.js 必须按 MAC 地址和设备名称去重扫描结果")
    check('find_item_by_identity' in scanner_src, "ble_scanner.c 必须按 MAC 地址和设备名称合并重复扫描广播")
    domain_template_src = (ROOT / "components" / "bikepower_domain" / "device_template.c").read_text(encoding="utf-8")
    check('device_template_list_add' in scanner_src or 'device_template_list_add' in domain_template_src, "设备模板保存必须复用领域层去重逻辑")
    check('scan_item_has_name' in scanner_src and 'continue;' in scanner_src, "ble_scanner.c 必须在序列化扫描结果时排除未命名设备")
    check('device_template_has_ride_service' in scanner_src, "ble_scanner.c 必须按功率/踏频/心率/FTMS 服务过滤扫描结果")
    check('filterRideDevices' in js and 'hasRideService' in js, "app.js 必须二次过滤无骑行服务的扫描结果")
    check('serviceBadges' in js and 'template-title-row' in js, "app.js 必须在设备名称行渲染多服务图标标签")
    check('template-expanded' in js and 'onTemplateToggle' in js, "已保存模板必须默认收起并支持点击展开详情")
    check('template-detail-grid' in js and 'template-compact-head' in css, "模板详情和紧凑标题必须有压缩布局样式")
    check('applied_template' in js and 'copyTemplateToState' in js, "应用模板后必须在页面展示复制后的厂商/型号/版本等设备信息")
    check('id="settings-applied-template-card"' in html, "复制后的设备信息必须展示在设置页设备信息分区")
    check('id="applied-template-card"' not in html, "复制后的设备信息不应展示在首页")
    check('applied_template_expanded' in js and 'onAppliedTemplateToggle' in js, "复制后的设备信息必须默认收起并支持点击展开")
    check('.svc-badge' in css and '.svc-power' in css and '.svc-cadence' in css and '.svc-hr' in css and '.svc-ftms' in css, "app.css 必须定义功率/踏频/心率/FTMS 标签样式")
    check('httpd_register_err_handler' in csrc and 'HTTPD_404_NOT_FOUND' in csrc, "wifi_portal.c 必须注册 404 兜底重定向以覆盖新系统探测路径")
    check('/.well-known/captive-portal' in csrc or 'CAPTIVE_PORTAL_API_PATH' in csrc, "wifi_portal.c 必须注册 RFC 8910 Captive Portal API 路由")
    check('application/captive+json' in csrc, "Captive Portal API 必须返回 application/captive+json")
    check('303 See Other' in captive_src, "Captive Portal 探测响应必须使用 303 See Other")
    check('Redirect to the captive portal' in captive_src, "Captive Portal 重定向响应必须带短 body 兼容 iOS CNA")
    check('shutdown(sock, 0)' in captive_src, "DNS 停止时必须 shutdown socket 以稳定唤醒 recvfrom")
    check('onTemplateSavePending' in js and 'onTemplateCancelPending' in js, "app.js 缺少扫描读取后保存/取消逻辑")
    check('onTemplateApply' in js and 'onTemplateDelete' in js, "app.js 缺少模板应用/删除逻辑")
    check('id="custom-pacer-name"' in html, "自定义配速机器人昵称输入缺失")
    check('custom-pacer-panel' in html and 'id="custom-target-watt"' in html,
          "自定义机器人必须使用紧凑窗口展示目标功率")
    check('id="custom-wkg-range"' in html and 'id="custom-watt-range"' in html,
          "自定义机器人窗口必须合并展示 W/kg 与功率区间")
    check('custom-pacer-panel' in css and 'custom-wkg-controls' in css,
          "app.css 缺少自定义机器人紧凑窗口样式")
    check('id="pacer-wkg"' not in html and 'id="pacer-watt"' not in html,
          "自定义机器人不应再用独立 info-row 显示功体比/功率区间")
    check('id="ride-power-editor"' in html and 'id="ride-power-float-row"' in html,
          "骑行固定功率输入和浮动范围必须有独立容器以便机器人模式隐藏")
    check('ride-power-editor' in js and 'ride-power-float-row' in js,
          "app.js 必须在机器人模式隐藏重复的功率输入/浮动范围")
    check('id="home-road"' not in html, "骑行配速机器人不应再使用独立 home-road 窗口")
    check('id="broadcast-toggle-bar"' in html, "首页顶部缺少广播数据多选项")
    check('data-broadcast="power"' in html and 'data-broadcast="cadence"' in html and 'data-broadcast="heartrate"' in html,
          "广播数据多选项必须包含功率/踏频/心率")
    check('onBroadcastToggle' in html and 'onBroadcastToggle' in js,
          "广播数据多选项必须绑定切换逻辑")
    check('broadcast_mask' in js and 'broadcastEnabled' in js and 'applyBroadcastVisibility' in js,
          "app.js 必须保存广播 mask 并按选择隐藏对应窗口")
    check('broadcast-toggle-bar' in css and 'broadcast-toggle' in css,
          "app.css 必须提供顶部广播多选项样式")
    check('切换骑行/跑步需点保存并关闭 WiFi' not in js,
          "普通保存必须允许切换骑行/跑步，并由固件即时重建 BLE GATT 表")
    check('active_sport_profile = STATE.sport_profile' in js,
          "保存成功后必须同步 active_sport_profile，避免误提示未保存")
    check('apply_json_fields(&next, body, true)' in csrc,
          "/api/config 必须允许 sport_profile 保存，不能只在关闭 WiFi 时生效")
    check('wifi_portal_ble_restart_requested' in csrc,
          "配网保持打开时也必须能请求 BLE 重启以重建 GATT 服务表")
    app_main_src = (ROOT / "main" / "app_main.c").read_text(encoding="utf-8")
    check('wifi_portal_ble_restart_requested()' in app_main_src
          and '运动 profile 已切换，重启 BLE 以重建 GATT 服务表' in app_main_src,
          "app_main 必须在主任务上下文处理运动 profile 切换后的 BLE 重启")
    check('conn == 0' in app_main_src and '等待 BLE 断开后重建 GATT 服务表' in app_main_src,
          "BLE 已连接时必须等待断开后再重建 GATT，避免强制踢掉客户端")
    check('id="ride-source-picker"' in html, "骑行页缺少固定功率/配速机器人胶囊条容器")
    check('id="ride-source-summary"' in html, "骑行页缺少当前骑行输出摘要")
    check('renderRideSourcePicker' in js and 'onRideSourceSelect' in js, "app.js 缺少骑行输出来源胶囊条渲染/切换逻辑")
    check('ride-source-pill' in css and 'ride-source-picker' in css and 'ride-source-summary' in css,
          "app.css 缺少骑行输出来源胶囊条/摘要样式")
    check('ride-source-card' not in css and 'ride-source-grid' not in css,
          "骑行输出来源不应继续使用大卡片网格样式")
    check('cycling-compact-card' in css and 'compact-hr-lock' in css,
          "骑行页必须提供一页内高密度布局样式")
    check('ride-inline-grid' not in html and 'ride-inline-field' not in html,
          "踏频和心率必须保持独立卡片，不应合并为双列区域")
    check('id="run-speed"' not in html, "跑步页不应保留独立跑步速度输入框")
    check('id="run-pace-min"' in html and 'id="run-pace-sec"' in html, "跑步页缺少配速分钟/秒输入")
    check('id="run-pace-presets"' in html, "跑步页缺少常用配速小卡片容器")
    check('id="run-pace-float"' in html and 'id="float-run-pace"' in html, "跑步页缺少配速浮动控件")
    check('id="run-cadence-float"' in html and 'id="float-run-cadence"' in html, "跑步页缺少步频浮动控件")
    for pace in ["3:00", "4:00", "5:00", "6:00", "7:00"]:
        check(f">{pace}<" in html, f"跑步页缺少 {pace}/km 配速小卡片")
    for key in [
        "paceSecFromSpeedX100",
        "speedX100FromPaceSec",
        "renderPaceFromState",
        "syncSpeedFromPace",
        "onRunPacePreset",
        "run_pace_float_sec",
        "run_cadence_float",
    ]:
        check(key in js, f"app.js 缺少跑步配速换算/浮动逻辑 {key}")
    check('id="about-dev"' not in html, "设备名称不应继续留在关于页")
    check('pager-track' not in html and 'pager-track' not in css, "关于内容不应继续使用横向分页")
    check('"/settings": renderSettings' in js, "app.js 路由缺少 /settings -> renderSettings")
    check('"/more":' not in js and '"/about":' not in js, "app.js 不应保留更多/关于作为一级路由")

    profile_pos = html.find('id="settings-profile"')
    device_pos = html.find('id="settings-device"')
    about_pos = html.find('id="settings-about"')
    applied_pos = html.find('id="settings-applied-template-card"')
    template_pos = html.find('id="settings-templates"')
    button_pos = html.find("物理按钮")
    check(device_pos != -1 and applied_pos != -1 and template_pos != -1 and device_pos < applied_pos < template_pos, "复制后的设备信息卡片必须位于设置页设备信息分区内")
    check(device_pos < button_pos < about_pos or about_pos < button_pos, "物理按钮入口或子页面必须位于设置区域")
    check(not (profile_pos < button_pos < device_pos), "物理按钮不应留在用户资料分区")
    check('id="settings-button-card"' not in html, "物理按钮不应继续使用折叠卡片")
    check('id="settings-led-card"' not in html, "LED 不应继续使用折叠卡片")
    check('toggleCollapse' not in js, "设置子页面改造后不应保留折叠卡片切换逻辑")
    legacy_collapsible_selectors = [
        ".collapsible-card",
        ".collapse-head",
        ".collapse-ico",
        ".collapse-body",
    ]
    for selector in legacy_collapsible_selectors:
        check(selector not in css, f"app.css 不应保留旧折叠卡片样式 {selector}")

    led_control_ids = [
        "settings-led-silent",
        "settings-led-wait-mode", "settings-led-wait-color",
        "settings-led-conn-mode", "settings-led-conn-color",
        "settings-led-wifi-mode", "settings-led-wifi-color",
        "settings-led-confirm-mode", "settings-led-confirm-color",
        "settings-led-error-mode", "settings-led-error-color",
    ]
    for ctl in led_control_ids:
        check(f'id="{ctl}"' in html, f"LED 自定义配置控件 {ctl} 缺失")
    check('id="settings-led-options"' in html, "LED 静默模式必须有可隐藏的模式/颜色选项容器")
    check('syncLedSilentVisibility' in js, "app.js 必须同步 LED 静默模式下的选项显隐")
    check('settings-led-silent' in js and 'change' in js, "LED 静默模式 checkbox 必须绑定 change 事件")
    check('POST' in portal_config_save_src and '/api/config' in portal_config_save_src and '/api/state' in portal_config_save_src,
          "test_portal_config_save.py 必须覆盖网页端保存后状态回显")
    check('DEFAULT_MINUTES = 3' in core_flow_endurance_src,
          "test_core_flow_endurance.py 必须默认执行 3 分钟核心流程耐久测试")
    check('simulate_button_flow' in core_flow_endurance_src and 'exercise_hr_following' in core_flow_endurance_src,
          "test_core_flow_endurance.py 必须覆盖模拟按钮与心率随动流程")
    check('exercise_web_edits' in core_flow_endurance_src and 'exercise_template_flow' in core_flow_endurance_src,
          "test_core_flow_endurance.py 必须覆盖网页编辑与设备模板流程")
    check('run_mock_portal_test' in test_runner_src and 'test_portal_config_save.py' in test_runner_src,
          "test_runner.sh 必须提供 mock 配网页保存测试入口")
    check('run_endurance_test' in test_runner_src and 'test_core_flow_endurance.py' in test_runner_src,
          "test_runner.sh 必须提供 3 分钟核心流程耐久测试入口")
    check('run_portal_test' in test_runner_src and 'stage5_portal_test.py' in test_runner_src,
          "test_runner.sh 必须提供配网门户 HTTP 自测入口")
    for key in [
        "led_silent",
        "led_wait_mode", "led_wait_color",
        "led_conn_mode", "led_conn_color",
        "led_wifi_mode", "led_wifi_color",
        "led_confirm_mode", "led_confirm_color",
        "led_error_mode", "led_error_color",
    ]:
        check(key in js, f"app.js 缺少 LED 配置字段 {key}")
    for key in ["custom_pacer_name", "syncCustomPacerOption", "customPacerDisplayName"]:
        check(key in js, f"app.js 缺少自定义配速昵称/下拉刷新逻辑 {key}")
    check('runpace:' in js and 'labelId: "float-run-pace"' in js,
          "跑步配速浮动按钮必须映射到实际显示节点 float-run-pace")
    check('runcadence:' in js and 'labelId: "float-run-cadence"' in js,
          "跑步步频浮动按钮必须映射到实际显示节点 float-run-cadence")

    # 总结
    print(f"\n{TAG} 结果: {g_pass} 通过, {g_fail} 失败")
    if g_fail == 0:
        print(f"{TAG} PASS: WiFi 门户静态结构全部通过")
        return 0
    else:
        print(f"{TAG} FAIL: 存在结构缺失，禁止编译/烧录", file=sys.stderr)
        for m in failures:
            print(f"{TAG}   - {m}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
