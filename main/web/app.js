/* BikePower 配网门户前端逻辑（iOS Captive Portal WebView 兼容：无 eval/无动态 script/无 ES6 模块） */
"use strict";

/* ============ 全局状态 ============ */
var STATE = {
  device_name: "",
  firmware_version: "",
  power: 0, cadence: 0, heartrate: 0,
  mode: "steady",
  power_float: 5, cadence_float: 2, hr_float: 2,
  power_step: 20,
  profile_weight: 0, profile_ftp: 0, profile_thr_hr: 0, profile_ride_type: "coco",
  custom_wkg_low: 25, custom_wkg_high: 30,
  custom_pacer_name: "自定义",
  led_silent: 0,
  led_wait_mode: 2, led_wait_color: 2,
  led_conn_mode: 1, led_conn_color: 1,
  led_wifi_mode: 2, led_wifi_color: 4,
  led_confirm_mode: 3, led_confirm_color: 3,
  led_error_mode: 3, led_error_color: 0,
  pacers: [],
  remaining_sec: 180,
  closing: false,
  hr_locked: false,
  settings_panel: "profile",
  device_name_editing: false,
  templates: [],
  templates_max: 5,
  template_delete_arm: -1,
  scanning: false,
  scan_devices: [],
  scan_timer: null,
  pending_template: null
};

/* 浮动幅度/步进范围（与 domain_config.h 一致；step 为单次加减量，默认 1） */
var FLOAT_RANGE = {
  power: { min: 0, max: 100, key: "power_float" },
  cadence: { min: 0, max: 20, key: "cadence_float" },
  hr: { min: 0, max: 20, key: "hr_float" },
  pstep: { min: 5, max: 50, key: "power_step", step: 5 }
};

var LED_FIELDS = [
  { id: "wait", modeKey: "led_wait_mode", colorKey: "led_wait_color", modeDefault: 2, colorDefault: 2 },
  { id: "conn", modeKey: "led_conn_mode", colorKey: "led_conn_color", modeDefault: 1, colorDefault: 1 },
  { id: "wifi", modeKey: "led_wifi_mode", colorKey: "led_wifi_color", modeDefault: 2, colorDefault: 4 },
  { id: "confirm", modeKey: "led_confirm_mode", colorKey: "led_confirm_color", modeDefault: 3, colorDefault: 3 },
  { id: "error", modeKey: "led_error_mode", colorKey: "led_error_color", modeDefault: 3, colorDefault: 0 }
];

/* ============ 工具函数 ============ */
function $(id) { return document.getElementById(id); }
function el(tag, attrs, html) {
  var e = document.createElement(tag);
  if (attrs) {
    for (var k in attrs) {
      if (k === "class") e.className = attrs[k];
      else if (k === "onclick") e.onclick = attrs[k];
      else e.setAttribute(k, attrs[k]);
    }
  }
  if (html != null) e.innerHTML = html;
  return e;
}
function toast(msg) {
  var t = $("toast");
  if (!t) return;
  t.textContent = msg;
  t.style.display = "block";
  setTimeout(function () { t.style.display = "none"; }, 1800);
}
function api(path, method, body, cb) {
  var x = new XMLHttpRequest();
  x.open(method || "GET", path, true);
  x.setRequestHeader("Connection", "close");
  if (body != null) x.setRequestHeader("Content-Type", "application/json");
  x.onreadystatechange = function () {
    if (x.readyState !== 4) return;
    var data = null;
    if (x.responseText && x.responseText.length > 0) {
      try { data = JSON.parse(x.responseText); } catch (e) { data = null; }
    }
    cb(x.status, data, x.responseText);
  };
  x.send(body != null ? JSON.stringify(body) : null);
}
function apiText(path, method, cb) {
  var x = new XMLHttpRequest();
  x.open(method || "GET", path, true);
  x.setRequestHeader("Connection", "close");
  x.onreadystatechange = function () {
    if (x.readyState !== 4) return;
    cb(x.status, x.responseText);
  };
  x.send(null);
}
function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }
function escapeHtml(s) {
  if (s == null) return "";
  return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/'/g, "&#39;");
}
function customPacerDisplayName() {
  var name = (STATE.custom_pacer_name || "").trim();
  return name || "自定义";
}
function syncCustomPacerOption() {
  var found = false;
  for (var i = 0; i < STATE.pacers.length; i++) {
    if (STATE.pacers[i].id === "custom") {
      STATE.pacers[i].name = customPacerDisplayName();
      STATE.pacers[i].wkg_low = STATE.custom_wkg_low / 10;
      STATE.pacers[i].wkg_high = STATE.custom_wkg_high / 10;
      STATE.pacers[i].custom = true;
      found = true;
      break;
    }
  }
  if (!found) {
    STATE.pacers.push({
      id: "custom",
      name: customPacerDisplayName(),
      wkg_low: STATE.custom_wkg_low / 10,
      wkg_high: STATE.custom_wkg_high / 10,
      custom: true
    });
  }
}
function intOr(v, fallback) {
  var n = parseInt(v, 10);
  return isNaN(n) ? fallback : n;
}
function setSelect(id, value) {
  var e = $(id);
  if (e) e.value = String(value);
}
function readLedControls() {
  var silent = $("settings-led-silent");
  if (silent) STATE.led_silent = silent.checked ? 1 : 0;
  for (var i = 0; i < LED_FIELDS.length; i++) {
    var f = LED_FIELDS[i];
    var mode = $("settings-led-" + f.id + "-mode");
    var color = $("settings-led-" + f.id + "-color");
    if (mode) STATE[f.modeKey] = clamp(intOr(mode.value, f.modeDefault), 0, 5);
    if (color) STATE[f.colorKey] = clamp(intOr(color.value, f.colorDefault), 0, 6);
  }
}
function renderLedControls() {
  var silent = $("settings-led-silent");
  if (silent) silent.checked = !!STATE.led_silent;
  for (var i = 0; i < LED_FIELDS.length; i++) {
    var f = LED_FIELDS[i];
    STATE[f.modeKey] = clamp(intOr(STATE[f.modeKey], f.modeDefault), 0, 5);
    STATE[f.colorKey] = clamp(intOr(STATE[f.colorKey], f.colorDefault), 0, 6);
    setSelect("settings-led-" + f.id + "-mode", STATE[f.modeKey]);
    setSelect("settings-led-" + f.id + "-color", STATE[f.colorKey]);
  }
}
function withLedConfig(body) {
  body.led_silent = STATE.led_silent ? 1 : 0;
  for (var i = 0; i < LED_FIELDS.length; i++) {
    var f = LED_FIELDS[i];
    body[f.modeKey] = STATE[f.modeKey];
    body[f.colorKey] = STATE[f.colorKey];
  }
  return body;
}

/* ============ 路由 ============ */
var ROUTES = {
  "/home": renderHome,
  "/settings": renderSettings,
  "/closing": renderClosing,
  "/success": renderSuccess
};
function normalizeRoute(hash) {
  if (hash === "/more" || hash === "/about") return "/settings";
  return hash;
}
function go(hash) {
  if (hash.charAt(0) === "#") hash = hash.slice(1);
  if (hash.charAt(0) !== "/") hash = "/" + hash;
  hash = normalizeRoute(hash);
  if (!ROUTES[hash]) hash = "/home";
  location.hash = hash;
  route();
}
function route() {
  captureDraft();
  var hash = location.hash.replace(/^#/, "");
  if (hash.charAt(0) !== "/") hash = "/" + hash;
  var normalized = normalizeRoute(hash);
  if (normalized !== hash) {
    location.hash = normalized;
    return;
  }
  hash = normalized;
  if (!ROUTES[hash]) hash = "/home";
  var views = document.querySelectorAll(".view");
  for (var i = 0; i < views.length; i++) views[i].classList.remove("active");
  var v = $("view" + hash.replace("/", "-"));
  if (v) v.classList.add("active");
  var items = document.querySelectorAll(".nav-item");
  for (var j = 0; j < items.length; j++) {
    var h = items[j].getAttribute("data-h");
    items[j].classList.toggle("on", h === hash);
  }
  ROUTES[hash]();
  window.scrollTo(0, 0);
}
function captureDraft() {
  var home = $("view-home");
  if (home && home.classList.contains("active")) {
    STATE.power = parseInt($("home-power").value, 10) || 0;
    STATE.cadence = parseInt($("home-cadence").value, 10) || 0;
    STATE.heartrate = parseInt($("home-hr").value, 10) || 0;
    var psel = $("home-pacer");
    if (psel && psel.value) STATE.profile_ride_type = psel.value;
    return;
  }
  var settings = $("view-settings");
  if (settings && settings.classList.contains("active")) {
    var weight = $("settings-weight");
    var ftp = $("settings-ftp");
    var thr = $("settings-thr");
    if (weight) STATE.profile_weight = parseInt(weight.value, 10) || 0;
    if (ftp) STATE.profile_ftp = parseInt(ftp.value, 10) || 0;
    if (thr) STATE.profile_thr_hr = parseInt(thr.value, 10) || 0;
    readLedControls();
    return;
  }
}

/* ============ 首页 ============ */
function renderHome() {
  $("home-ver").textContent = "v" + STATE.firmware_version;
  $("home-power").value = STATE.power;
  $("home-cadence").value = STATE.cadence;
  $("home-hr").value = STATE.heartrate;
  $("float-power").textContent = STATE.power_float;
  $("float-cadence").textContent = STATE.cadence_float;
  $("float-hr").textContent = STATE.hr_float;
  applyHrLockState();
  updateZoneBadges();
  renderPacerSel();
  applyModeView();
}
/* 按当前 mode 显示固定/机器人区块与分段选中态 */
function applyModeView() {
  var isRoad = STATE.mode === "road";
  var isErg = STATE.mode === "erg";
  var steady = $("home-steady");
  var road = $("home-road");
  if (steady) steady.style.display = isRoad ? "none" : "block";
  if (road) road.style.display = isRoad ? "block" : "none";
  var ergNote = $("erg-note");
  if (ergNote) ergNote.style.display = isErg ? "block" : "none";
  /* 分段选中：erg 归到固定模式一侧高亮（steady），因为 erg 由 App 驱动 */
  var items = document.querySelectorAll("#mode-seg .seg-item");
  for (var i = 0; i < items.length; i++) {
    var m = items[i].getAttribute("data-mode");
    var on = (m === "road") ? isRoad : !isRoad;
    items[i].classList.toggle("on", on);
  }
  updatePacerInfo();
}
/* 用户点击分段切换模式（仅改本地 STATE，保存时才落盘） */
function onModeSwitch(mode) {
  captureDraft();
  STATE.mode = mode;
  applyModeView();
}
function onFloatAdj(kind, delta) {
  var r = FLOAT_RANGE[kind];
  if (!r) return;
  var stepSize = r.step || 1;
  var cur = STATE[r.key] || 0;
  var next = clamp(cur + delta * stepSize, r.min, r.max);
  STATE[r.key] = next;
  var lbl = $("float-" + kind);
  if (lbl) lbl.textContent = next;
}
function renderPacerSel() {
  var sel = $("home-pacer");
  if (!sel) return;
  syncCustomPacerOption();
  sel.innerHTML = "";
  for (var i = 0; i < STATE.pacers.length; i++) {
    var p = STATE.pacers[i];
    var o = el("option", { value: p.id },
      p.name + " (" + p.wkg_low.toFixed(1) + "-" + p.wkg_high.toFixed(1) + " W/kg)");
    if (p.id === STATE.profile_ride_type) o.selected = true;
    sel.appendChild(o);
  }
}
function onPacerChange() {
  var sel = $("home-pacer");
  if (sel && sel.value) STATE.profile_ride_type = sel.value;
  updatePacerInfo();
}
/* 自定义档 W/kg 加减（步进 0.1 W/kg = ×10 的 1） */
function onCustomWkgAdj(which, delta) {
  var lo = STATE.custom_wkg_low, hi = STATE.custom_wkg_high;
  if (which === "low") {
    lo = clamp(lo + delta, 10, 70);
    if (lo > hi) hi = lo;
  } else {
    hi = clamp(hi + delta, 10, 70);
    if (hi < lo) lo = hi;
  }
  STATE.custom_wkg_low = lo;
  STATE.custom_wkg_high = hi;
  syncCustomPacerOption();
  renderPacerSel();
  updatePacerInfo();
}
function onCustomNameInput() {
  var inp = $("custom-pacer-name");
  if (!inp) return;
  STATE.custom_pacer_name = (inp.value || "").trim();
  syncCustomPacerOption();
  renderPacerSel();
  updatePacerInfo();
}
function updatePacerInfo() {
  var wkgEl = $("pacer-wkg");
  var wattEl = $("pacer-watt");
  var isCustom = STATE.profile_ride_type === "custom";
  /* 自定义档编辑区显隐 */
  var box = $("custom-wkg-box");
  if (box) box.style.display = isCustom ? "block" : "none";
  if (isCustom) {
    var loEl = $("cwkg-low"), hiEl = $("cwkg-high");
    if (loEl) loEl.textContent = (STATE.custom_wkg_low / 10).toFixed(1);
    if (hiEl) hiEl.textContent = (STATE.custom_wkg_high / 10).toFixed(1);
    var nameEl = $("custom-pacer-name");
    if (nameEl && document.activeElement !== nameEl) nameEl.value = customPacerDisplayName();
  }
  if (!wkgEl || !wattEl) return;
  var lo10, hi10;
  if (isCustom) {
    lo10 = STATE.custom_wkg_low; hi10 = STATE.custom_wkg_high;
  } else {
    var p = null;
    for (var i = 0; i < STATE.pacers.length; i++) {
      if (STATE.pacers[i].id === STATE.profile_ride_type) { p = STATE.pacers[i]; break; }
    }
    if (!p) { wkgEl.textContent = "—"; wattEl.textContent = "—"; return; }
    lo10 = Math.round(p.wkg_low * 10); hi10 = Math.round(p.wkg_high * 10);
  }
  var w = STATE.profile_weight || 70;
  wkgEl.textContent = (lo10 / 10).toFixed(1) + " - " + (hi10 / 10).toFixed(1) + " W/kg";
  wattEl.textContent = Math.round(lo10 * w / 10) + " - " + Math.round(hi10 * w / 10) + " W";
}
function applyHrLockState() {
  var radios = document.getElementsByName("hr-lock");
  for (var i = 0; i < radios.length; i++) {
    radios[i].checked = (radios[i].value === "1") === STATE.hr_locked;
  }
  var hrInput = $("home-hr");
  if (hrInput) hrInput.disabled = !STATE.hr_locked;
  var hrFloatRow = $("hr-float-row");
  if (hrFloatRow) hrFloatRow.style.display = STATE.hr_locked ? "flex" : "none";
}
function onHrLockChange() {
  var radios = document.getElementsByName("hr-lock");
  for (var i = 0; i < radios.length; i++) {
    if (radios[i].checked) {
      STATE.hr_locked = (radios[i].value === "1");
      break;
    }
  }
  var hrInput = $("home-hr");
  if (hrInput) hrInput.disabled = !STATE.hr_locked;
  var hrFloatRow = $("hr-float-row");
  if (hrFloatRow) hrFloatRow.style.display = STATE.hr_locked ? "flex" : "none";
}
function updateZoneBadges() {
  var pwr = parseInt($("home-power").value, 10) || 0;
  var hr = parseInt($("home-hr").value, 10) || 0;
  var ftp = STATE.profile_ftp || 200;
  var thr = STATE.profile_thr_hr || 180;
  var pct = ftp > 0 ? Math.round(pwr * 100 / ftp) : 0;
  var pZone = zoneFromPct(pct);
  var hZone = zoneFromHr(hr, thr);
  setZone("zone-pwr", pZone);
  setZone("zone-hr", hZone);
}
function zoneFromPct(pct) {
  if (pct === 0) return { name: "Z?", color: "#c7c7cc" };
  if (pct < 55) return { name: "Z1", color: "#9ca3af" };
  if (pct < 75) return { name: "Z2", color: "#22c55e" };
  if (pct < 90) return { name: "Z3", color: "#0ea5e9" };
  if (pct < 100) return { name: "Z4", color: "#f59e0b" };
  if (pct < 115) return { name: "Z5", color: "#ef4444" };
  return { name: "Z6", color: "#a21caf" };
}
function zoneFromHr(hr, thr) {
  if (hr === 0) return { name: "Z?", color: "#c7c7cc" };
  var pct = thr > 0 ? Math.round(hr * 100 / thr) : 0;
  if (pct < 60) return { name: "Z1", color: "#9ca3af" };
  if (pct < 75) return { name: "Z2", color: "#22c55e" };
  if (pct < 85) return { name: "Z3", color: "#0ea5e9" };
  if (pct < 95) return { name: "Z4", color: "#f59e0b" };
  if (pct < 100) return { name: "Z5", color: "#ef4444" };
  return { name: "Z6", color: "#a21caf" };
}
function setZone(id, z) {
  var e = $(id);
  if (!e) return;
  e.textContent = z.name;
  var dot = e.querySelector(".dot");
  if (dot) dot.style.background = z.color;
}
function collectConfig() {
  return withLedConfig({
    device_name: STATE.device_name,
    mode: STATE.mode,
    power: clamp(parseInt($("home-power").value, 10) || 0, 0, 2000),
    cadence: clamp(parseInt($("home-cadence").value, 10) || 0, 20, 120),
    heartrate: clamp(parseInt($("home-hr").value, 10) || 0, 60, 200),
    hr_lock: STATE.hr_locked ? 1 : 0,
    power_float: STATE.power_float,
    cadence_float: STATE.cadence_float,
    hr_float: STATE.hr_float,
    power_step: STATE.power_step,
    profile_weight: STATE.profile_weight,
    profile_ftp: STATE.profile_ftp,
    profile_thr_hr: STATE.profile_thr_hr,
    profile_ride_type: STATE.profile_ride_type,
    custom_wkg_low: STATE.custom_wkg_low,
    custom_wkg_high: STATE.custom_wkg_high,
    custom_pacer_name: customPacerDisplayName()
  });
}
function onSave() {
  var btn = $("home-save");
  btn.disabled = true;
  api("/api/config", "POST", collectConfig(), function (status, data) {
    btn.disabled = false;
    if (status === 200 && data && data.ok) {
      toast("已保存");
    } else {
      toast("保存失败: " + ((data && data.error) || status));
    }
  });
}
var s_closeArmed = false;
var s_closeTimer = null;
function onCloseWifi() {
  var btn = $("home-close");
  /* iOS Captive Portal WebView 不支持 confirm()，改用二次点击确认 */
  if (!s_closeArmed) {
    s_closeArmed = true;
    btn.classList.add("warn");
    toast("再次点击确认关闭 WiFi");
    if (s_closeTimer) clearTimeout(s_closeTimer);
    s_closeTimer = setTimeout(function () {
      s_closeArmed = false;
      btn.classList.remove("warn");
    }, 3000);
    return;
  }
  if (s_closeTimer) { clearTimeout(s_closeTimer); s_closeTimer = null; }
  s_closeArmed = false;
  btn.classList.remove("warn");
  btn.disabled = true;
  api("/api/close-wifi", "POST", collectConfig(), function (status, data) {
    btn.disabled = false;
    if (status === 200 && data && data.ok) {
      go("/closing");
    } else {
      toast("操作失败: " + ((data && data.error) || status));
    }
  });
}

/* ============ 设置 ============ */
function renderSettings() {
  $("home-ver").textContent = "v" + STATE.firmware_version;
  $("settings-weight").value = STATE.profile_weight;
  $("settings-ftp").value = STATE.profile_ftp;
  $("settings-thr").value = STATE.profile_thr_hr;
  applyDeviceNameView();
  $("settings-version").textContent = "v" + STATE.firmware_version;
  var ps = $("float-pstep");
  if (ps) ps.textContent = STATE.power_step;
  renderLedControls();
  if (STATE.settings_panel === "device") loadTemplates();
  else renderTemplates();
  applySettingsPanel();
}
function applyDeviceNameView() {
  var display = $("device-name-display");
  var viewRow = $("device-name-view-row");
  var editRow = $("device-name-edit-row");
  var input = $("settings-device-name");
  var name = STATE.device_name || "BikePower";
  if (display) display.textContent = name;
  if (input && !STATE.device_name_editing) input.value = name;
  if (viewRow) viewRow.style.display = STATE.device_name_editing ? "none" : "flex";
  if (editRow) editRow.style.display = STATE.device_name_editing ? "flex" : "none";
}
function loadTemplates() {
  api("/api/device-templates", "GET", null, function (status, data) {
    if (status === 200 && data) {
      STATE.templates = data.items || [];
      STATE.templates_max = data.max || 5;
      renderTemplates();
    }
  });
}
function templateTitle(t) {
  return (t.alias || t.device_name || t.adv_name || "未命名设备").trim();
}
function hasDeviceName(t) {
  return !!(t && ((t.alias && t.alias.trim()) || (t.device_name && t.device_name.trim()) || (t.adv_name && t.adv_name.trim())));
}
function hasRideService(flags) {
  flags = flags || 0;
  return !!(flags & (4 | 8 | 16 | 32));
}
function filterNamedDevices(items) {
  var out = [];
  items = items || [];
  for (var i = 0; i < items.length; i++) {
    if (hasDeviceName(items[i])) out.push(items[i]);
  }
  return out;
}
function filterRideDevices(items) {
  var out = [];
  items = filterNamedDevices(items);
  for (var i = 0; i < items.length; i++) {
    if (hasRideService(items[i].service_flags)) out.push(items[i]);
  }
  return out;
}
function serviceBadges(flags) {
  var html = "";
  flags = flags || 0;
  if (flags & 4) html += '<span class="svc-badge svc-power" title="功率">⚡</span>';
  if (flags & 16) html += '<span class="svc-badge svc-cadence" title="踏频">↻</span>';
  if (flags & 8) html += '<span class="svc-badge svc-hr" title="心率">♥</span>';
  if (flags & 32) html += '<span class="svc-badge svc-ftms" title="智能骑行台">FT</span>';
  return html;
}
function templateSubtitle(t) {
  var parts = [];
  if (t.manufacturer) parts.push(t.manufacturer);
  if (t.model) parts.push(t.model);
  if (t.firmware) parts.push("固件 " + t.firmware);
  return parts.length ? parts.join(" · ") : "暂无厂商/型号/版本信息";
}
function renderTemplates() {
  var count = $("template-count");
  if (count) count.textContent = String(STATE.templates.length) + "/" + String(STATE.templates_max || 5);
  var box = $("template-list");
  if (!box) return;
  var html = "";
  if (STATE.pending_template) {
    var p = STATE.pending_template;
    html += '<div class="template-item scan-pending"><div class="template-title">确认保存设备信息</div>';
    html += '<div class="template-sub">' + escapeHtml(templateSubtitle(p)) + '</div>';
    html += '<div class="template-meta">地址 ' + escapeHtml(p.addr || "—") + ' · RSSI ' + escapeHtml(p.last_rssi != null ? String(p.last_rssi) : "—") + ' dBm</div>';
    html += '<div class="field"><label>模板别名</label><input id="template-alias-input" maxlength="23" value="' + escapeHtml(templateTitle(p)) + '"></div>';
    html += '<div class="template-actions"><button class="mini-btn primary" onclick="onTemplateSavePending()">保存模板</button>';
    html += '<button class="mini-btn" onclick="onTemplateCancelPending()">取消</button></div></div>';
  }
  if (STATE.scan_devices && STATE.scan_devices.length > 0) {
    html += '<div class="template-section-title">扫描结果</div>';
    for (var s = 0; s < STATE.scan_devices.length; s++) {
      var d = STATE.scan_devices[s];
      html += '<div class="template-item scan-result">';
      html += '<div class="template-main"><div class="template-title-row"><div class="template-title">' + escapeHtml(templateTitle(d)) + '</div><div class="svc-badges">' + serviceBadges(d.service_flags) + '</div></div>';
      html += '<div class="template-sub">' + escapeHtml(serviceSummary(d.service_flags)) + '</div>';
      html += '<div class="template-meta">地址 ' + escapeHtml(d.addr || "—") + ' · RSSI ' + escapeHtml(d.last_rssi != null ? String(d.last_rssi) : "—") + ' dBm</div></div>';
      html += '<div class="template-actions"><button class="mini-btn primary" onclick="onTemplateReadDevice(' + s + ')">读取信息</button></div>';
      html += '</div>';
    }
  }
  if (!STATE.templates || STATE.templates.length === 0) {
    if (!html) html = '<div class="template-empty">暂无模板。点击上方按钮扫描附近蓝牙设备，读取设备信息后保存。</div>';
    box.innerHTML = html;
    return;
  }
  if (html) html += '<div class="template-section-title">已保存模板</div>';
  for (var i = 0; i < STATE.templates.length; i++) {
    var t = STATE.templates[i];
    html += '<div class="template-item">';
    html += '<div class="template-main"><div class="template-title">' + escapeHtml(templateTitle(t)) + '</div>';
    html += '<div class="template-sub">' + escapeHtml(templateSubtitle(t)) + '</div>';
    html += '<div class="template-meta">地址 ' + escapeHtml(t.addr || "—") + ' · RSSI ' + escapeHtml(t.last_rssi != null ? String(t.last_rssi) : "—") + ' dBm</div></div>';
    html += '<div class="template-actions"><button class="mini-btn primary" onclick="onTemplateApply(' + t.id + ')">应用到本设备</button>';
    html += '<button class="mini-btn danger" onclick="onTemplateDelete(' + t.id + ')">' + (STATE.template_delete_arm === t.id ? '确认删除' : '删除') + '</button></div>';
    html += '</div>';
  }
  box.innerHTML = html;
}
function serviceSummary(flags) {
  var parts = [];
  flags = flags || 0;
  if (flags & 1) parts.push("设备信息");
  if (flags & 2) parts.push("电量");
  if (flags & 4) parts.push("功率");
  if (flags & 8) parts.push("心率");
  if (flags & 16) parts.push("踏频");
  if (flags & 32) parts.push("FTMS");
  return parts.length ? ("服务：" + parts.join(" / ")) : "未识别标准服务，仍可尝试读取设备信息";
}
function stopScanPoll() {
  if (STATE.scan_timer) {
    clearInterval(STATE.scan_timer);
    STATE.scan_timer = null;
  }
}
function pollScanResults() {
  api("/api/ble-scan/results", "GET", null, function (status, data) {
    if (status !== 200 || !data) return;
    STATE.scanning = !!data.scanning;
    STATE.scan_devices = filterRideDevices(data.devices);
    renderTemplates();
    var note = $("template-scan-note");
    if (note) {
      note.style.display = "block";
      note.textContent = STATE.scanning ? ("扫描中，已发现 " + STATE.scan_devices.length + " 个设备…") : ("扫描完成，发现 " + STATE.scan_devices.length + " 个设备。点击“读取信息”连接设备并读取基础信息。");
    }
    if (!STATE.scanning) stopScanPoll();
  });
}
function onTemplateScan() {
  var note = $("template-scan-note");
  if (note) {
    note.style.display = "block";
    note.textContent = "正在扫描附近蓝牙设备…";
  }
  STATE.pending_template = null;
  STATE.scan_devices = [];
  stopScanPoll();
  api("/api/ble-scan/start", "POST", {}, function (status, data) {
    if (status === 200 && data && data.ok) {
      STATE.scanning = true;
      toast("已开始扫描");
      pollScanResults();
      STATE.scan_timer = setInterval(pollScanResults, 1000);
      setTimeout(function () { if (STATE.scanning) onTemplateStopScan(); }, 9000);
    } else {
      toast("扫描启动失败: " + ((data && data.error) || status));
    }
  });
}
function onTemplateStopScan() {
  api("/api/ble-scan/stop", "POST", {}, function (status, data) {
    STATE.scanning = false;
    stopScanPoll();
    pollScanResults();
    toast((status === 200 && data && data.ok) ? "已停止扫描" : "停止扫描失败");
  });
}
function onTemplateReadDevice(index) {
  var d = STATE.scan_devices[index];
  if (!d) return;
  toast("正在连接并读取设备信息…");
  api("/api/ble-scan/read", "POST", { addr: d.addr, addr_type: d.addr_type || 0 }, function (status, data) {
    if (status === 200 && data && data.ok && data.template) {
      STATE.pending_template = data.template;
      if (!STATE.pending_template.adv_name) STATE.pending_template.adv_name = d.adv_name || "";
      if (!STATE.pending_template.last_rssi) STATE.pending_template.last_rssi = d.last_rssi || 0;
      STATE.pending_template.service_flags = (STATE.pending_template.service_flags || 0) | (d.service_flags || 0);
      renderTemplates();
      toast("已读取设备信息，请确认别名后保存");
    } else {
      toast("读取失败: " + ((data && data.error) || status));
    }
  });
}
function onTemplateSavePending() {
  if (!STATE.pending_template) return;
  var alias = $("template-alias-input");
  var tpl = STATE.pending_template;
  tpl.alias = alias ? alias.value.trim() : templateTitle(tpl);
  if (!tpl.alias) {
    toast("模板别名不能为空");
    return;
  }
  api("/api/device-templates/save", "POST", tpl, function (status, data) {
    if (status === 200 && data && data.ok) {
      STATE.pending_template = null;
      toast("模板已保存");
      loadTemplates();
    } else {
      toast("保存失败: " + ((data && data.error) || status));
    }
  });
}
function onTemplateCancelPending() {
  STATE.pending_template = null;
  renderTemplates();
}
function onTemplateApply(id) {
  api("/api/device-templates/apply", "POST", { id: id }, function (status, data) {
    if (status === 200 && data && data.ok) {
      toast("模板已应用，设备名称已更新");
      api("/api/state", "GET", null, function (s, st) {
        if (s === 200 && st) {
          STATE.device_name = st.device_name || STATE.device_name;
          applyDeviceNameView();
        }
      });
    } else {
      toast("应用失败: " + ((data && data.error) || status));
    }
  });
}
function onTemplateDelete(id) {
  if (STATE.template_delete_arm !== id) {
    STATE.template_delete_arm = id;
    renderTemplates();
    toast("再次点击确认删除模板");
    setTimeout(function () {
      if (STATE.template_delete_arm === id) {
        STATE.template_delete_arm = -1;
        renderTemplates();
      }
    }, 3000);
    return;
  }
  STATE.template_delete_arm = -1;
  api("/api/device-templates/delete", "POST", { id: id }, function (status, data) {
    if (status === 200 && data && data.ok) {
      toast("模板已删除");
      loadTemplates();
    } else {
      toast("删除失败: " + ((data && data.error) || status));
    }
  });
}
function onDeviceNameEdit() {
  STATE.device_name_editing = true;
  applyDeviceNameView();
  var input = $("settings-device-name");
  if (input) {
    input.value = STATE.device_name || "BikePower";
    input.focus();
    input.select();
  }
}
function onDeviceNameConfirm() {
  var input = $("settings-device-name");
  var name = input ? (input.value || "").trim() : "";
  if (!name) {
    toast("设备名称不能为空");
    return false;
  }
  if (name.length > 20) {
    toast("设备名称最多 20 字符");
    return false;
  }
  STATE.device_name = name;
  STATE.device_name_editing = false;
  applyDeviceNameView();
  toast("设备名称已确认，保存后生效");
  return true;
}
function onDeviceNameCancel() {
  STATE.device_name_editing = false;
  applyDeviceNameView();
}
function toggleCollapse(id) {
  var card = $(id);
  if (!card) return;
  card.classList.toggle("collapsed");
}
function onSettingsPanel(panel) {
  captureDraft();
  if (panel !== "profile" && panel !== "device" && panel !== "about") panel = "profile";
  STATE.settings_panel = panel;
  applySettingsPanel();
}
function applySettingsPanel() {
  var tabs = document.querySelectorAll(".settings-tab");
  for (var i = 0; i < tabs.length; i++) {
    var p = tabs[i].getAttribute("data-panel");
    tabs[i].classList.toggle("on", p === STATE.settings_panel);
  }
  var panels = document.querySelectorAll(".settings-panel");
  for (var j = 0; j < panels.length; j++) {
    panels[j].classList.toggle("on", panels[j].id === "settings-" + STATE.settings_panel);
  }
  var save = $("settings-save");
  if (save) save.style.display = STATE.settings_panel === "about" ? "none" : "block";
}
function onSettingsSave() {
  var btn = $("settings-save");
  btn.disabled = true;
  STATE.profile_weight = clamp(parseInt($("settings-weight").value, 10) || 0, 40, 150);
  STATE.profile_ftp = clamp(parseInt($("settings-ftp").value, 10) || 0, 50, 500);
  STATE.profile_thr_hr = clamp(parseInt($("settings-thr").value, 10) || 0, 100, 220);
  readLedControls();
  if (STATE.device_name_editing) {
    var confirmed = onDeviceNameConfirm();
    if (confirmed === false) {
      btn.disabled = false;
      return;
    }
  }
  var name = (STATE.device_name || "").trim();
  if (!name) {
    btn.disabled = false;
    toast("设备名称不能为空");
    onSettingsPanel("device");
    return;
  }
  if (name.length > 20) {
    btn.disabled = false;
    toast("设备名称最多 20 字符");
    onSettingsPanel("device");
    return;
  }
  STATE.device_name = name;
  applyDeviceNameView();
  var body = collectConfigBase();
  api("/api/config", "POST", body, function (status, data) {
    btn.disabled = false;
    if (status === 200 && data && data.ok) {
      toast("设置已保存");
    } else {
      toast("保存失败: " + ((data && data.error) || status));
    }
  });
}
/* 设置页保存用：不依赖首页 DOM，取当前 STATE 全量字段 */
function collectConfigBase() {
  return withLedConfig({
    device_name: STATE.device_name,
    mode: STATE.mode,
    power: STATE.power,
    cadence: STATE.cadence,
    heartrate: STATE.heartrate,
    hr_lock: STATE.hr_locked ? 1 : 0,
    power_float: STATE.power_float,
    cadence_float: STATE.cadence_float,
    hr_float: STATE.hr_float,
    power_step: STATE.power_step,
    profile_weight: STATE.profile_weight,
    profile_ftp: STATE.profile_ftp,
    profile_thr_hr: STATE.profile_thr_hr,
    profile_ride_type: STATE.profile_ride_type,
    custom_wkg_low: STATE.custom_wkg_low,
    custom_wkg_high: STATE.custom_wkg_high,
    custom_pacer_name: customPacerDisplayName()
  });
}

/* ============ 关闭提示 ============ */
function renderClosing() {
  var box = $("closing-countdown");
  if (box) box.textContent = "5";
  var n = 5;
  var timer = setInterval(function () {
    n--;
    if (box) box.textContent = String(n);
    if (n <= 0) {
      clearInterval(timer);
      var tip = $("closing-tip");
      if (tip) tip.innerHTML = "WiFi 已关闭，设备已返回骑行模式";
    }
  }, 1000);
}

/* ============ 成功页 ============ */
function renderSuccess() {
  /* 由 close-wifi 流程跳转，与 closing 类似，但显示成功消息 */
}

/* ============ 倒计时 / 续期 ============ */
var timePollTimer = null;
function startTimePoll() {
  if (timePollTimer) return;
  timePollTimer = setInterval(pollTime, 5000);
  pollTime();
}
function pollTime() {
  apiText("/api/time", "GET", function (status, text) {
    if (status !== 200) return;
    var sec = parseInt(text, 10);
    if (isNaN(sec)) return;
    STATE.remaining_sec = sec;
    var badge = $("badge-time");
    if (badge) badge.textContent = sec + "s";
    var renewBtn = $("renew-btn");
    if (renewBtn) renewBtn.style.display = (sec <= 60) ? "block" : "none";
    if (sec <= 0 && !STATE.closing) {
      STATE.closing = true;
      go("/closing");
    }
  });
}
function onRenew() {
  apiText("/api/renew", "POST", function (status, text) {
    if (status === 200) {
      var sec = parseInt(text, 10);
      if (!isNaN(sec)) {
        STATE.remaining_sec = sec;
        toast("已续期 " + sec + "s");
        pollTime();
      }
    } else {
      toast("续期失败");
    }
  });
}

/* ============ 初始化 ============ */
function init() {
  /* 绑定导航 */
  var items = document.querySelectorAll(".nav-item");
  for (var i = 0; i < items.length; i++) {
    items[i].onclick = (function (h) { return function () { go(h); }; })(items[i].getAttribute("data-h"));
  }
  /* 拉取初始状态 */
  api("/api/state", "GET", null, function (status, data) {
    if (status === 200 && data) {
      STATE.device_name = data.device_name || "BikePower";
      STATE.firmware_version = data.firmware_version || "";
      STATE.power = data.power || 0;
      STATE.cadence = data.cadence || 0;
      STATE.heartrate = data.heartrate || 0;
      STATE.mode = data.mode || "steady";
      STATE.power_float = data.power_float != null ? data.power_float : 5;
      STATE.cadence_float = data.cadence_float != null ? data.cadence_float : 2;
      STATE.hr_float = data.hr_float != null ? data.hr_float : 2;
      STATE.power_step = data.power_step != null ? data.power_step : 20;
      STATE.profile_weight = data.profile_weight || 70;
      STATE.profile_ftp = data.profile_ftp || 200;
      STATE.profile_thr_hr = data.profile_thr_hr || 180;
      STATE.profile_ride_type = data.profile_ride_type || "coco";
      STATE.custom_wkg_low = data.custom_wkg_low != null ? data.custom_wkg_low : 25;
      STATE.custom_wkg_high = data.custom_wkg_high != null ? data.custom_wkg_high : 30;
      STATE.custom_pacer_name = data.custom_pacer_name || "自定义";
      STATE.led_silent = data.led_silent != null ? data.led_silent : 0;
      STATE.led_wait_mode = data.led_wait_mode != null ? data.led_wait_mode : 2;
      STATE.led_wait_color = data.led_wait_color != null ? data.led_wait_color : 2;
      STATE.led_conn_mode = data.led_conn_mode != null ? data.led_conn_mode : 1;
      STATE.led_conn_color = data.led_conn_color != null ? data.led_conn_color : 1;
      STATE.led_wifi_mode = data.led_wifi_mode != null ? data.led_wifi_mode : 2;
      STATE.led_wifi_color = data.led_wifi_color != null ? data.led_wifi_color : 4;
      STATE.led_confirm_mode = data.led_confirm_mode != null ? data.led_confirm_mode : 3;
      STATE.led_confirm_color = data.led_confirm_color != null ? data.led_confirm_color : 3;
      STATE.led_error_mode = data.led_error_mode != null ? data.led_error_mode : 3;
      STATE.led_error_color = data.led_error_color != null ? data.led_error_color : 0;
      STATE.pacers = data.pacers || [];
      syncCustomPacerOption();
      STATE.remaining_sec = data.remaining_sec != null ? data.remaining_sec : 180;
      STATE.closing = !!data.closing;
      STATE.hr_locked = !!data.hr_locked;
      loadTemplates();
      startTimePoll();
      route();
    } else {
      document.body.innerHTML = '<div class="result-card"><div class="icon warn">!</div><h1>初始化失败</h1><div class="sub">无法连接 /api/state，请重试或重启设备。</div></div>';
    }
  });
  /* hashchange 路由 */
  window.addEventListener("hashchange", route);
  /* 输入校验：实时高亮超界值 */
  function bindInputRange(id, lo, hi) {
    var inp = $(id);
    if (!inp) return;
    inp.addEventListener("input", function () {
      var v = parseInt(inp.value, 10);
      inp.classList.toggle("out-of-range", isNaN(v) || v < lo || v > hi);
      if (id === "home-power" || id === "home-hr") updateZoneBadges();
    });
  }
  bindInputRange("home-power", 0, 2000);
  bindInputRange("home-cadence", 20, 120);
  bindInputRange("home-hr", 60, 200);
  bindInputRange("settings-weight", 40, 150);
  bindInputRange("settings-ftp", 50, 500);
  bindInputRange("settings-thr", 100, 220);
}
if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", init);
} else {
  init();
}
