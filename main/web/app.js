/* BikePower 配网门户前端逻辑（iOS Captive Portal WebView 兼容：无 eval/无动态 script/无 ES6 模块） */
"use strict";

/* ============ 全局状态 ============ */
var STATE = {
  device_name: "",
  firmware_version: "",
  firmware_name: "",
  board: "",
  git_sha: "",
  build_time: "",
  sport_profile: "cycling",
  active_sport_profile: "cycling",
  broadcast_mask: 7,
  power: 0, cadence: 0, heartrate: 0,
  run_speed_x100: 1000, run_pace_float_sec: 10, run_cadence: 170, run_cadence_float: 4,
  run_mode: "fixed", run_pacer_type: "jog", run_pace_low_sec: 390, run_pace_high_sec: 450,
  run_custom_name: "自定义", run_incline_x10: 0,
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
  run_pacers: [],
  remaining_sec: 180,
  closing: false,
  hr_locked: false,
  settings_panel: "profile",
  device_name_editing: false,
  templates: [],
  templates_max: 5,
  template_delete_arm: -1,
  template_expanded: {},
  template_analysis: {},
  scanning: false,
  scan_devices: [],
  scan_timer: null,
  pending_template: null,
  applied_template: null,
  applied_template_expanded: false
};

/* 浮动幅度/步进范围（与 domain_config.h 一致；step 为单次加减量，默认 1） */
var FLOAT_RANGE = {
  power: { min: 0, max: 100, key: "power_float" },
  cadence: { min: 0, max: 20, key: "cadence_float" },
  hr: { min: 0, max: 20, key: "hr_float" },
  runpace: { min: 0, max: 60, key: "run_pace_float_sec", step: 5, labelId: "float-run-pace" },
  runcadence: { min: 0, max: 20, key: "run_cadence_float", labelId: "float-run-cadence" },
  pstep: { min: 5, max: 50, key: "power_step", step: 5 }
};

var LED_FIELDS = [
  { id: "wait", modeKey: "led_wait_mode", colorKey: "led_wait_color", modeDefault: 2, colorDefault: 2 },
  { id: "conn", modeKey: "led_conn_mode", colorKey: "led_conn_color", modeDefault: 1, colorDefault: 1 },
  { id: "wifi", modeKey: "led_wifi_mode", colorKey: "led_wifi_color", modeDefault: 2, colorDefault: 4 },
  { id: "confirm", modeKey: "led_confirm_mode", colorKey: "led_confirm_color", modeDefault: 3, colorDefault: 3 },
  { id: "error", modeKey: "led_error_mode", colorKey: "led_error_color", modeDefault: 3, colorDefault: 0 }
];

var BROADCAST_BITS = { power: 1, cadence: 2, heartrate: 4 };

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
function runCustomDisplayName() {
  var name = (STATE.run_custom_name || "").trim();
  return name || "自定义";
}
function syncRunPacerOption() {
  var found = false;
  for (var i = 0; i < STATE.run_pacers.length; i++) {
    if (STATE.run_pacers[i].id === "custom") {
      STATE.run_pacers[i].name = runCustomDisplayName();
      STATE.run_pacers[i].pace_low_sec = STATE.run_pace_low_sec;
      STATE.run_pacers[i].pace_high_sec = STATE.run_pace_high_sec;
      STATE.run_pacers[i].cadence = STATE.run_cadence;
      STATE.run_pacers[i].custom = true;
      found = true;
      break;
    }
  }
  if (!found) {
    STATE.run_pacers.push({
      id: "custom",
      name: runCustomDisplayName(),
      pace_low_sec: STATE.run_pace_low_sec,
      pace_high_sec: STATE.run_pace_high_sec,
      cadence: STATE.run_cadence,
      custom: true
    });
  }
}
function intOr(v, fallback) {
  var n = parseInt(v, 10);
  return isNaN(n) ? fallback : n;
}
function paceSecFromSpeedX100(speedX100) {
  speedX100 = clamp(parseInt(speedX100, 10) || 1000, 300, 2500);
  return Math.round(360000 / speedX100);
}
function speedX100FromPaceSec(paceSec) {
  paceSec = clamp(parseInt(paceSec, 10) || 360, 144, 1200);
  return clamp(Math.round(360000 / paceSec), 300, 2500);
}
function formatPaceSec(paceSec) {
  paceSec = clamp(parseInt(paceSec, 10) || 360, 144, 1200);
  var min = Math.floor(paceSec / 60);
  var sec = paceSec % 60;
  return min + ":" + (sec < 10 ? "0" : "") + sec;
}
function paceSecText(sec) {
  return formatPaceSec(sec) + " /km";
}
function speedTextFromPaceSec(sec) {
  return (speedX100FromPaceSec(sec) / 100).toFixed(2) + " km/h";
}
function estimateHrFromPower(power) {
  var ftp = STATE.profile_ftp || 200;
  var thr = STATE.profile_thr_hr || 180;
  power = clamp(parseInt(power, 10) || 0, 0, 2000);
  var pct = ftp > 0 ? Math.floor(power * 100 / ftp) : 0;
  var hrPct;
  if (pct <= 55) hrPct = 42 + Math.floor(pct * 26 / 55);
  else if (pct <= 75) hrPct = 68 + Math.floor((pct - 55) * 15 / 20);
  else if (pct <= 90) hrPct = 83 + Math.floor((pct - 75) * 11 / 15);
  else if (pct <= 105) hrPct = 94 + Math.floor((pct - 90) * 6 / 15);
  else if (pct <= 120) hrPct = 100 + Math.floor((pct - 105) * 6 / 15);
  else hrPct = 106;
  return clamp(Math.floor(thr * hrPct / 100), 60, 200);
}
function runningEquivalentPower(speedX100) {
  speedX100 = clamp(parseInt(speedX100, 10) || 1000, 300, 2500);
  var progress = Math.floor((speedX100 - 300) * 1000 / (2500 - 300));
  var ftp = STATE.profile_ftp || 200;
  var pct = 35 + Math.floor(progress * 85 / 1000);
  return clamp(Math.floor(ftp * pct / 100), 0, 2000);
}
function estimateLinkedHeartrate() {
  if (STATE.sport_profile === "running") {
    return estimateHrFromPower(runningEquivalentPower(STATE.run_speed_x100));
  }
  return estimateHrFromPower($("home-power") ? $("home-power").value : STATE.power);
}
function applyLinkedHrPreview() {
  if (directHrControl() || STATE.hr_locked) return;
  var hr = estimateLinkedHeartrate();
  var input = STATE.sport_profile === "running" ? $("run-hr") : $("home-hr");
  if (input) input.value = hr;
  STATE.heartrate = hr;
}
function renderPaceFromState() {
  var minEl = $("run-pace-min");
  var secEl = $("run-pace-sec");
  var label = $("run-pace-speed");
  if (!minEl || !secEl) return;
  var speedX100 = clamp(parseInt(STATE.run_speed_x100, 10) || 1000, 300, 2500);
  STATE.run_speed_x100 = speedX100;
  var paceSec = paceSecFromSpeedX100(speedX100);
  minEl.value = Math.floor(paceSec / 60);
  secEl.value = paceSec % 60 < 10 ? "0" + (paceSec % 60) : String(paceSec % 60);
  if (label) label.textContent = (speedX100 / 100).toFixed(2) + " km/h";
}
function syncSpeedFromPace() {
  if (STATE.run_mode === "pacer") {
    updateRunPacerInfo();
    return;
  }
  var minEl = $("run-pace-min");
  var secEl = $("run-pace-sec");
  var label = $("run-pace-speed");
  if (!minEl || !secEl) return;
  var paceSec = clamp((parseInt(minEl.value, 10) || 0) * 60 + (parseInt(secEl.value, 10) || 0), 144, 1200);
  var speedX100 = speedX100FromPaceSec(paceSec);
  STATE.run_speed_x100 = speedX100;
  minEl.value = Math.floor(paceSec / 60);
  secEl.value = paceSec % 60 < 10 ? "0" + (paceSec % 60) : String(paceSec % 60);
  if (label) label.textContent = (speedX100 / 100).toFixed(2) + " km/h";
  applyLinkedHrPreview();
  updateZoneBadges();
}
function onRunPacePreset(min, sec) {
  var minEl = $("run-pace-min");
  var secEl = $("run-pace-sec");
  if (!minEl || !secEl) return;
  minEl.value = min;
  secEl.value = sec < 10 ? "0" + sec : String(sec);
  syncSpeedFromPace();
}
function renderRunSourcePicker() {
  var picker = $("run-source-picker");
  if (!picker) return;
  syncRunPacerOption();
  var html = '<button class="ride-source-pill" data-source="fixed" onclick="onRunSourceSelect(\'fixed\')">固定</button>';
  for (var i = 0; i < STATE.run_pacers.length; i++) {
    var p = STATE.run_pacers[i];
    html += '<button class="ride-source-pill" data-source="' + escapeHtml(p.id) + '" onclick="onRunSourceSelect(\'' + escapeHtml(p.id) + '\')">' + escapeHtml(p.name) + '</button>';
  }
  picker.innerHTML = html;
}
function selectedRunPacer() {
  syncRunPacerOption();
  var id = STATE.run_pacer_type || "jog";
  for (var i = 0; i < STATE.run_pacers.length; i++) {
    if (STATE.run_pacers[i].id === id) return STATE.run_pacers[i];
  }
  return STATE.run_pacers.length ? STATE.run_pacers[0] : null;
}
function onRunSourceSelect(source) {
  captureDraft();
  if (source === "fixed") {
    STATE.run_mode = "fixed";
  } else {
    STATE.run_mode = "pacer";
    STATE.run_pacer_type = source || "jog";
    var p = selectedRunPacer();
    if (p && p.id !== "custom") {
      STATE.run_pace_low_sec = p.pace_low_sec;
      STATE.run_pace_high_sec = p.pace_high_sec;
      STATE.run_cadence = p.cadence || STATE.run_cadence;
    }
  }
  renderRunSourcePicker();
  updateRunPacerInfo();
}
function onRunCustomPaceAdj(which, delta) {
  var lo = clamp(STATE.run_pace_low_sec + (which === "low" ? delta : 0), 144, 1200);
  var hi = clamp(STATE.run_pace_high_sec + (which === "high" ? delta : 0), 144, 1200);
  if (lo > hi) {
    if (which === "low") hi = lo;
    else lo = hi;
  }
  STATE.run_pace_low_sec = lo;
  STATE.run_pace_high_sec = hi;
  syncRunPacerOption();
  updateRunPacerInfo();
}
function onRunCustomNameInput() {
  var inp = $("run-custom-name");
  if (!inp) return;
  STATE.run_custom_name = (inp.value || "").trim();
  syncRunPacerOption();
  renderRunSourcePicker();
  updateRunPacerInfo();
}
function onRunInclineAdj(delta) {
  STATE.run_incline_x10 = clamp((STATE.run_incline_x10 || 0) + delta, -100, 150);
  updateRunPacerInfo();
}
function updateRunPacerInfo() {
  var isPacer = STATE.run_mode === "pacer";
  var isCustom = isPacer && STATE.run_pacer_type === "custom";
  var fixed = $("run-fixed-panel");
  var custom = $("run-custom-panel");
  var summary = $("run-source-summary");
  if (fixed) fixed.style.display = isPacer ? "none" : "block";
  if (custom) custom.style.display = isCustom ? "block" : "none";
  var pills = document.querySelectorAll("#run-source-picker .ride-source-pill");
  var selectedSource = isPacer ? STATE.run_pacer_type : "fixed";
  for (var i = 0; i < pills.length; i++) {
    pills[i].classList.toggle("selected", pills[i].getAttribute("data-source") === selectedSource);
  }
  if (!isPacer) {
    if (summary) summary.innerHTML = '<span>已选固定配速</span><b>按填写配速输出</b>';
  } else {
    var p = selectedRunPacer();
    if (p) {
      var lo = isCustom ? STATE.run_pace_low_sec : p.pace_low_sec;
      var hi = isCustom ? STATE.run_pace_high_sec : p.pace_high_sec;
      var cad = isCustom ? STATE.run_cadence : p.cadence;
      if (summary) summary.innerHTML = '<span>已选' + escapeHtml(p.name) + '</span><b>' + escapeHtml(paceSecText(lo)) + ' - ' + escapeHtml(paceSecText(hi)) + '</b>';
      if (!isCustom) STATE.run_cadence = cad || STATE.run_cadence;
    }
  }
  var nameEl = $("run-custom-name");
  if (nameEl && document.activeElement !== nameEl) nameEl.value = runCustomDisplayName();
  var lowEl = $("run-pace-low"), highEl = $("run-pace-high");
  if (lowEl) lowEl.textContent = formatPaceSec(STATE.run_pace_low_sec);
  if (highEl) highEl.textContent = formatPaceSec(STATE.run_pace_high_sec);
  var rangeEl = $("run-custom-pace-range");
  if (rangeEl) rangeEl.textContent = paceSecText(STATE.run_pace_low_sec) + " - " + paceSecText(STATE.run_pace_high_sec);
  var speedEl = $("run-custom-speed");
  if (speedEl) speedEl.textContent = (speedX100FromPaceSec(Math.round((STATE.run_pace_low_sec + STATE.run_pace_high_sec) / 2)) / 100).toFixed(1);
  var cadEl = $("run-custom-cadence-text");
  if (cadEl) cadEl.textContent = String(STATE.run_cadence || 170) + " spm";
  var incline = $("run-incline");
  if (incline) incline.textContent = ((STATE.run_incline_x10 || 0) / 10).toFixed(1);
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
function syncLedSilentVisibility() {
  var silent = $("settings-led-silent");
  var options = $("settings-led-options");
  var isSilent = silent ? silent.checked : !!STATE.led_silent;
  if (options) options.hidden = isSilent;
  for (var i = 0; i < LED_FIELDS.length; i++) {
    var f = LED_FIELDS[i];
    var mode = $("settings-led-" + f.id + "-mode");
    var color = $("settings-led-" + f.id + "-color");
    if (mode) mode.disabled = isSilent;
    if (color) color.disabled = isSilent;
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
  syncLedSilentVisibility();
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
  "/settings/profile": renderSettings,
  "/settings/device": renderSettings,
  "/settings/templates": renderSettings,
  "/settings/button": renderSettings,
  "/settings/led": renderSettings,
  "/settings/about": renderSettings,
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
  var v = hash.indexOf("/settings") === 0 ? $("view-settings") : $("view" + hash.replace("/", "-"));
  if (v) v.classList.add("active");
  var items = document.querySelectorAll(".nav-item");
  for (var j = 0; j < items.length; j++) {
    var h = items[j].getAttribute("data-h");
    items[j].classList.toggle("on", h === hash || (h === "/settings" && hash.indexOf("/settings") === 0));
  }
  ROUTES[hash]();
  window.scrollTo(0, 0);
}
function captureDraft() {
  var home = $("view-home");
  if (home && home.classList.contains("active")) {
    if ($("home-power")) STATE.power = parseInt($("home-power").value, 10) || 0;
    if ($("home-cadence")) STATE.cadence = parseInt($("home-cadence").value, 10) || 0;
    if (STATE.sport_profile === "running" && $("run-hr")) {
      STATE.heartrate = parseInt($("run-hr").value, 10) || 0;
    } else if ($("home-hr")) {
      STATE.heartrate = parseInt($("home-hr").value, 10) || 0;
    }
    if ($("run-pace-min") && $("run-pace-sec")) {
      STATE.run_speed_x100 = speedX100FromPaceSec((parseInt($("run-pace-min").value, 10) || 0) * 60 + (parseInt($("run-pace-sec").value, 10) || 0));
    }
    if ($("run-cadence")) STATE.run_cadence = parseInt($("run-cadence").value, 10) || 0;
    var runName = $("run-custom-name");
    if (runName) STATE.run_custom_name = (runName.value || "").trim();
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
function firmwareLabel() {
  return STATE.firmware_name || ("v" + STATE.firmware_version);
}
function firmwareShortLabel() {
  var full = firmwareLabel();
  var board = STATE.board || "";
  if (!board && full) {
    var mBoard = full.match(/^([a-z0-9]+(?:-[a-z0-9]+)*?)-v\d/i);
    if (mBoard) board = mBoard[1];
  }
  var ver = STATE.firmware_version ? ("v" + STATE.firmware_version.replace(/^v/, "")) : "";
  if (!ver && full) {
    var mVer = full.match(/v\d+\.\d+\.\d+/i);
    if (mVer) ver = mVer[0];
  }
  var git = STATE.git_sha || "";
  if (!git && full) {
    var mGit = full.match(/-g([0-9a-f]{6,12})/i);
    if (mGit) git = "g" + mGit[1];
  } else if (git && git.charAt(0) !== "g") {
    git = "g" + git;
  }
  var parts = [];
  if (board) parts.push(board);
  if (ver) parts.push(ver);
  if (git) parts.push(git);
  return parts.length ? parts.join(" · ") : full;
}
function renderHome() {
  $("home-ver").textContent = firmwareShortLabel();
  $("home-ver").title = firmwareLabel();
  $("home-power").value = STATE.power;
  $("home-cadence").value = STATE.cadence;
  $("home-hr").value = STATE.heartrate;
  $("run-hr").value = STATE.heartrate;
  renderPaceFromState();
  $("run-cadence").value = STATE.run_cadence;
  $("float-power").textContent = STATE.power_float;
  $("float-cadence").textContent = STATE.cadence_float;
  $("float-hr").textContent = STATE.hr_float;
  $("float-run-hr").textContent = STATE.hr_float;
  $("float-run-pace").textContent = STATE.run_pace_float_sec;
  $("float-run-cadence").textContent = STATE.run_cadence_float;
  applyHrLockState();
  updateZoneBadges();
  renderRideSourcePicker();
  renderRunSourcePicker();
  applySportView();
  applyModeView();
  updateRunPacerInfo();
  applyBroadcastVisibility();
}
function templateIdentityKey(t) {
  if (!t) return "";
  if (t.addr) return "addr:" + String(t.addr_type || 0) + ":" + String(t.addr).toUpperCase();
  var name = t.device_name || t.adv_name || t.alias || "";
  return name ? ("name:" + String(name).toLowerCase()) : "";
}
function findByTemplateIdentity(items, t) {
  var key = templateIdentityKey(t);
  if (!key) return -1;
  for (var i = 0; i < items.length; i++) {
    if (templateIdentityKey(items[i]) === key) return i;
  }
  return -1;
}
function filterUniqueDevices(items) {
  var out = [];
  items = items || [];
  for (var i = 0; i < items.length; i++) {
    var it = items[i];
    var idx = findByTemplateIdentity(out, it);
    if (idx < 0) {
      out.push(it);
    } else {
      out[idx].service_flags = (out[idx].service_flags || 0) | (it.service_flags || 0);
      if (!out[idx].adv_name && it.adv_name) out[idx].adv_name = it.adv_name;
      if (!out[idx].device_name && it.device_name) out[idx].device_name = it.device_name;
      if (!out[idx].alias && it.alias) out[idx].alias = it.alias;
      if ((it.last_rssi || 0) > (out[idx].last_rssi || -127)) out[idx].last_rssi = it.last_rssi;
    }
  }
  return out;
}
function copyTemplateToState(t) {
  if (!t) return null;
  var copy = {};
  var keys = ["id", "alias", "adv_name", "device_name", "manufacturer", "model", "firmware", "hardware", "software", "addr", "addr_type", "last_rssi", "service_flags"];
  for (var i = 0; i < keys.length; i++) copy[keys[i]] = t[keys[i]];
  STATE.applied_template = copy;
  STATE.applied_template_expanded = false;
  STATE.device_name = copy.device_name || copy.adv_name || copy.alias || STATE.device_name;
  return copy;
}
function renderAppliedTemplate() {
  var box = $("settings-applied-template-card");
  if (!box) return;
  var t = STATE.applied_template;
  if (!t) {
    box.style.display = "none";
    box.innerHTML = "";
    return;
  }
  var expanded = !!STATE.applied_template_expanded;
  var html = '<button class="applied-compact-head" onclick="onAppliedTemplateToggle()"><div><div class="applied-label">已复制模板设备信息</div><div class="applied-name">' + escapeHtml(templateTitle(t)) + '</div></div><div class="svc-badges">' + serviceBadges(t.service_flags) + '</div><span class="chev">⌄</span></button>';
  if (expanded) html += '<div class="template-detail-grid applied-detail-grid">' + templateDetailCells(t) + '</div>';
  box.className = "card applied-template-card" + (expanded ? " applied-template-expanded" : " applied-template-collapsed");
  box.innerHTML = html;
  box.style.display = "block";
}
function onAppliedTemplateToggle() {
  STATE.applied_template_expanded = !STATE.applied_template_expanded;
  renderAppliedTemplate();
}
/* 按当前 mode 更新骑行输出来源与辅助文案 */
function applyModeView() {
  var isRoad = STATE.mode === "road";
  var isErg = STATE.mode === "erg";
  var isRunning = STATE.sport_profile === "running";
  var steady = $("home-steady");
  if (steady) steady.style.display = isRunning ? "none" : "block";
  var ergNote = $("erg-note");
  if (ergNote) ergNote.style.display = (!isRunning && isErg) ? "block" : "none";
  var selectedSource = isRoad ? STATE.profile_ride_type : "steady";
  var items = document.querySelectorAll("#ride-source-picker .ride-source-pill");
  var selectedItem = null;
  for (var i = 0; i < items.length; i++) {
    var source = items[i].getAttribute("data-source");
    var selected = source === selectedSource;
    items[i].classList.toggle("selected", selected);
    if (selected) selectedItem = items[i];
  }
  if (selectedItem && selectedItem.scrollIntoView) selectedItem.scrollIntoView({ block: "nearest", inline: "center" });
  updatePacerInfo();
  applyBroadcastVisibility();
}
function applySportView() {
  var isRunning = STATE.sport_profile === "running";
  var cycling = $("home-steady");
  var running = $("home-running");
  if (cycling) cycling.style.display = isRunning ? "none" : "block";
  if (running) running.style.display = isRunning ? "block" : "none";
  var items = document.querySelectorAll("#sport-seg .seg-item");
  for (var i = 0; i < items.length; i++) {
    var s = items[i].getAttribute("data-sport");
    items[i].classList.toggle("on", s === STATE.sport_profile);
  }
  if (isRunning) updateRunPacerInfo();
  applyBroadcastVisibility();
}
function onSportSwitch(sport) {
  captureDraft();
  STATE.sport_profile = sport === "running" ? "running" : "cycling";
  applySportView();
  applyModeView();
}
function broadcastEnabled(kind) {
  return (STATE.broadcast_mask & (BROADCAST_BITS[kind] || 0)) !== 0;
}
function onlyHeartrateBroadcast() {
  return (STATE.broadcast_mask & 7) === BROADCAST_BITS.heartrate;
}
function directHrControl() {
  return onlyHeartrateBroadcast();
}
function onBroadcastToggle(kind, checked) {
  var bit = BROADCAST_BITS[kind] || 0;
  if (!bit) return;
  var next = checked ? (STATE.broadcast_mask | bit) : (STATE.broadcast_mask & ~bit);
  if ((next & 7) === 0) {
    toast("至少保留一个数据项");
    var input = document.querySelector('#broadcast-toggle-bar [data-broadcast="' + kind + '"] input');
    if (input) input.checked = true;
    return;
  }
  STATE.broadcast_mask = next & 7;
  applyBroadcastVisibility();
}
function showIf(id, visible) {
  var el = $(id);
  if (el) el.style.display = visible ? "" : "none";
}
function applyBroadcastVisibility() {
  var powerLabel = $("broadcast-power-label");
  var cadenceLabel = $("broadcast-cadence-label");
  var isRunning = STATE.sport_profile === "running";
  if (powerLabel) powerLabel.textContent = isRunning ? "配速" : "功率";
  if (cadenceLabel) cadenceLabel.textContent = isRunning ? "步频" : "踏频";
  var toggles = document.querySelectorAll("#broadcast-toggle-bar .broadcast-toggle");
  for (var i = 0; i < toggles.length; i++) {
    var kind = toggles[i].getAttribute("data-broadcast");
    var input = toggles[i].querySelector("input");
    var on = broadcastEnabled(kind);
    toggles[i].classList.toggle("on", on);
    if (input) input.checked = on;
  }
  showIf("cycling-power-card", !isRunning && broadcastEnabled("power"));
  showIf("cycling-cadence-card", !isRunning && broadcastEnabled("cadence"));
  showIf("cycling-hr-card", !isRunning && broadcastEnabled("heartrate"));
  showIf("running-speed-card", isRunning && broadcastEnabled("power"));
  showIf("running-cadence-card", isRunning && broadcastEnabled("cadence"));
  showIf("running-hr-card", isRunning && broadcastEnabled("heartrate"));
  applyHrLockState();
}
/* 用户点击分段切换模式（仅改本地 STATE，保存时才落盘） */
function onModeSwitch(mode) {
  captureDraft();
  STATE.mode = mode;
  applyModeView();
}
function onRideSourceSelect(source) {
  captureDraft();
  if (source === "steady") {
    STATE.mode = "steady";
  } else {
    STATE.mode = "road";
    STATE.profile_ride_type = source || "custom";
  }
  renderRideSourcePicker();
  applyModeView();
}
function onFloatAdj(kind, delta) {
  var r = FLOAT_RANGE[kind];
  if (!r) return;
  var stepSize = r.step || 1;
  var cur = STATE[r.key] || 0;
  var next = clamp(cur + delta * stepSize, r.min, r.max);
  STATE[r.key] = next;
  var lbl = $(r.labelId || ("float-" + kind));
  if (lbl) lbl.textContent = next;
}
function renderRideSourcePicker() {
  var picker = $("ride-source-picker");
  if (!picker) return;
  syncCustomPacerOption();
  var html = '<button class="ride-source-pill" data-source="steady" onclick="onRideSourceSelect(\'steady\')">固定</button>';
  for (var i = 0; i < STATE.pacers.length; i++) {
    var p = STATE.pacers[i];
    html += '<button class="ride-source-pill" data-source="' + escapeHtml(p.id) + '" onclick="onRideSourceSelect(\'' + escapeHtml(p.id) + '\')">' + escapeHtml(p.name) + '</button>';
  }
  picker.innerHTML = html;
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
  renderRideSourcePicker();
  updatePacerInfo();
}
function onCustomNameInput() {
  var inp = $("custom-pacer-name");
  if (!inp) return;
  STATE.custom_pacer_name = (inp.value || "").trim();
  syncCustomPacerOption();
  renderRideSourcePicker();
  updatePacerInfo();
}
function updatePacerInfo() {
  var isRoad = STATE.mode === "road";
  var isCustom = isRoad && STATE.profile_ride_type === "custom";
  var powerInput = $("home-power");
  var powerLabel = $("ride-power-label");
  var powerEditor = $("ride-power-editor");
  var powerFloatRow = $("ride-power-float-row");
  var sourceSummary = $("ride-source-summary");
  var robotNote = $("ride-robot-note");
  if (powerInput) powerInput.disabled = isRoad;
  if (powerLabel) powerLabel.textContent = isRoad ? "机器人功率" : "固定功率";
  if (powerEditor) powerEditor.style.display = isRoad ? "none" : "block";
  if (powerFloatRow) powerFloatRow.style.display = isRoad ? "none" : "flex";
  if (robotNote) robotNote.style.display = isRoad ? "block" : "none";
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
  if (!isRoad) {
    if (sourceSummary) sourceSummary.innerHTML = '<span>已选固定功率</span><b>按填写功率输出</b>';
    return;
  }
  var lo10, hi10;
  var sourceName = customPacerDisplayName();
  if (isCustom) {
    lo10 = STATE.custom_wkg_low; hi10 = STATE.custom_wkg_high;
  } else {
    var p = null;
    for (var i = 0; i < STATE.pacers.length; i++) {
      if (STATE.pacers[i].id === STATE.profile_ride_type) { p = STATE.pacers[i]; break; }
    }
    if (!p) return;
    sourceName = p.name;
    lo10 = Math.round(p.wkg_low * 10); hi10 = Math.round(p.wkg_high * 10);
  }
  var w = STATE.profile_weight || 70;
  var wkgText = (lo10 / 10).toFixed(1) + " - " + (hi10 / 10).toFixed(1) + " W/kg";
  var wattText = Math.round(lo10 * w / 10) + " - " + Math.round(hi10 * w / 10) + " W";
  var mid = Math.round(((lo10 + hi10) / 2) * w / 10);
  STATE.power = mid;
  if (isCustom) {
    var wkgRangeEl = $("custom-wkg-range"), wattRangeEl = $("custom-watt-range"), targetEl = $("custom-target-watt");
    if (wkgRangeEl) wkgRangeEl.textContent = wkgText;
    if (wattRangeEl) wattRangeEl.textContent = wattText;
    if (targetEl) targetEl.textContent = mid;
    if (sourceSummary) sourceSummary.innerHTML = '<span>已选' + escapeHtml(sourceName) + '</span><b>自定义范围</b>';
  } else if (sourceSummary) {
    sourceSummary.innerHTML = '<span>已选' + escapeHtml(sourceName) + '</span><b>' + escapeHtml(wkgText) + ' · ' + escapeHtml(wattText) + '</b>';
  }
  if (powerInput) powerInput.value = mid;
  applyLinkedHrPreview();
  updateZoneBadges();
}
function applyHrLockState() {
  var direct = directHrControl();
  var radios = document.getElementsByName("hr-lock");
  for (var i = 0; i < radios.length; i++) {
    radios[i].checked = (radios[i].value === "1") === STATE.hr_locked;
  }
  var runRadios = document.getElementsByName("run-hr-lock");
  for (var r = 0; r < runRadios.length; r++) {
    runRadios[r].checked = (runRadios[r].value === "1") === STATE.hr_locked;
  }
  var hrLockRows = document.querySelectorAll("#cycling-hr-card .hr-lock-row,#running-hr-card .hr-lock-row");
  for (var h = 0; h < hrLockRows.length; h++) {
    hrLockRows[h].style.display = direct ? "none" : "";
  }
  var hrInput = $("home-hr");
  if (hrInput) hrInput.disabled = direct ? false : !STATE.hr_locked;
  var runHrInput = $("run-hr");
  if (runHrInput) runHrInput.disabled = direct ? false : !STATE.hr_locked;
  var hrFloatRow = $("hr-float-row");
  if (hrFloatRow) hrFloatRow.style.display = (direct || STATE.hr_locked) ? "flex" : "none";
  var runHrFloatRow = $("run-hr-float-row");
  if (runHrFloatRow) runHrFloatRow.style.display = (direct || STATE.hr_locked) ? "flex" : "none";
  applyLinkedHrPreview();
  updateZoneBadges();
}
function onHrLockChange() {
  if (directHrControl()) {
    applyHrLockState();
    return;
  }
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
  applyHrLockState();
}
function onRunHrLockChange() {
  if (directHrControl()) {
    applyHrLockState();
    return;
  }
  var radios = document.getElementsByName("run-hr-lock");
  for (var i = 0; i < radios.length; i++) {
    if (radios[i].checked) {
      STATE.hr_locked = (radios[i].value === "1");
      break;
    }
  }
  applyHrLockState();
}
function updateZoneBadges() {
  applyLinkedHrPreview();
  var pwr = parseInt($("home-power").value, 10) || 0;
  var hr = STATE.sport_profile === "running"
    ? (parseInt($("run-hr").value, 10) || 0)
    : (parseInt($("home-hr").value, 10) || 0);
  var ftp = STATE.profile_ftp || 200;
  var thr = STATE.profile_thr_hr || 180;
  var pct = ftp > 0 ? Math.round(pwr * 100 / ftp) : 0;
  var pZone = zoneFromPct(pct);
  var hZone = zoneFromHr(hr, thr);
  setZone("zone-pwr", pZone);
  setZone("zone-hr", hZone);
  setZone("zone-run-hr", hZone);
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
  syncSpeedFromPace();
  return withLedConfig({
    device_name: STATE.device_name,
    sport_profile: STATE.sport_profile,
    broadcast_mask: STATE.broadcast_mask & 7,
    mode: STATE.mode,
    power: clamp(parseInt($("home-power").value, 10) || 0, 0, 2000),
    cadence: clamp(parseInt($("home-cadence").value, 10) || 0, 20, 120),
    heartrate: clamp(parseInt(STATE.sport_profile === "running" ? $("run-hr").value : $("home-hr").value, 10) || 0, 60, 200),
    run_speed_x100: STATE.run_speed_x100,
    run_pace_float_sec: STATE.run_pace_float_sec,
    run_cadence: clamp(parseInt($("run-cadence").value, 10) || 0, 120, 220),
    run_cadence_float: STATE.run_cadence_float,
    run_mode: STATE.run_mode,
    run_pacer_type: STATE.run_pacer_type,
    run_pace_low_sec: STATE.run_pace_low_sec,
    run_pace_high_sec: STATE.run_pace_high_sec,
    run_custom_name: STATE.run_custom_name,
    run_incline_x10: STATE.run_incline_x10,
    hr_lock: directHrControl() ? 1 : (STATE.hr_locked ? 1 : 0),
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
      STATE.active_sport_profile = STATE.sport_profile;
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

function copyTextFallback(text) {
  var area = document.createElement("textarea");
  area.value = text;
  area.setAttribute("readonly", "readonly");
  area.style.position = "fixed";
  area.style.left = "-9999px";
  document.body.appendChild(area);
  area.select();
  var ok = false;
  try { ok = document.execCommand("copy"); } catch (e) { ok = false; }
  document.body.removeChild(area);
  return ok;
}
function onCopyUpgradeUrl() {
  var urlEl = $("upgrade-url");
  var url = urlEl ? urlEl.textContent : "https://easyhey.com/?firmware=MOD220";
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(url).then(function () {
      toast("升级地址已复制");
    }, function () {
      toast(copyTextFallback(url) ? "升级地址已复制" : "复制失败，请长按地址手动复制");
    });
    return;
  }
  toast(copyTextFallback(url) ? "升级地址已复制" : "复制失败，请长按地址手动复制");
}

/* ============ 设置 ============ */
function renderSettings() {
  syncSettingsPanelFromRoute();
  $("home-ver").textContent = firmwareShortLabel();
  $("home-ver").title = firmwareLabel();
  $("settings-weight").value = STATE.profile_weight;
  $("settings-ftp").value = STATE.profile_ftp;
  $("settings-thr").value = STATE.profile_thr_hr;
  applyDeviceNameView();
  var aboutVer = $("about-version");
  if (aboutVer) {
    aboutVer.textContent = firmwareShortLabel();
    aboutVer.title = firmwareLabel();
  }
  var aboutBoard = $("about-board");
  if (aboutBoard) aboutBoard.textContent = STATE.board || "-";
  var buildTime = $("settings-build-time");
  if (buildTime) buildTime.textContent = STATE.build_time || "-";
  var ps = $("float-pstep");
  if (ps) ps.textContent = STATE.power_step;
  renderLedControls();
  renderAppliedTemplate();
  if (STATE.settings_panel === "templates") loadTemplates();
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
function safeDeviceNameText(s) {
  if (s == null) return "";
  s = String(s).trim();
  if (!s) return "";
  var readable = 0;
  for (var i = 0; i < s.length; i++) {
    var code = s.charCodeAt(i);
    if ((code >= 0x30 && code <= 0x39) ||
        (code >= 0x41 && code <= 0x5a) ||
        (code >= 0x61 && code <= 0x7a) ||
        (code >= 0x4e00 && code <= 0x9fff)) {
      readable++;
    }
  }
  if (readable === 0 || s.indexOf("□") >= 0 || s.indexOf("\ufffd") >= 0) return "";
  return s;
}
function readableDeviceName(t) {
  if (!t) return "外部运动设备";
  var name = safeDeviceNameText(t.alias) || safeDeviceNameText(t.device_name) || safeDeviceNameText(t.adv_name);
  return name || "外部运动设备";
}
function templateTitle(t) {
  return readableDeviceName(t);
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
  return filterUniqueDevices(out);
}
function serviceBadges(flags) {
  var html = "";
  flags = flags || 0;
  if (flags & 4) html += '<span class="svc-badge svc-power" title="功率">⚡</span>';
  if (flags & 16) html += '<span class="svc-badge svc-cadence" title="踏频">↻</span>';
  if (flags & 8) html += '<span class="svc-badge svc-hr" title="心率">♥</span>';
  if (flags & 32) html += '<span class="svc-badge svc-ftms" title="智能骑行台">智能</span>';
  return html;
}
function templateSubtitle(t) {
  var parts = [];
  if (t.manufacturer) parts.push(t.manufacturer);
  if (t.model) parts.push(t.model);
  if (t.firmware) parts.push("固件 " + t.firmware);
  return parts.length ? parts.join(" · ") : "暂无厂商/型号/版本信息";
}
function templateDetailCells(t) {
  var rows = [
    ["设备名", safeDeviceNameText(t.device_name) || safeDeviceNameText(t.adv_name) || "外部运动设备"],
    ["别名", safeDeviceNameText(t.alias) || "—"],
    ["MAC", t.addr || "—"],
    ["厂商", t.manufacturer || "—"],
    ["型号", t.model || "—"],
    ["固件", t.firmware || "—"],
    ["硬件", t.hardware || "—"],
    ["软件", t.software || "—"],
    ["服务", serviceSummary(t.service_flags).replace(/^服务：/, "")],
    ["RSSI", (t.last_rssi != null ? String(t.last_rssi) + " dBm" : "—")]
  ];
  var html = "";
  for (var i = 0; i < rows.length; i++) {
    html += '<div class="template-detail-cell"><span>' + escapeHtml(rows[i][0]) + '</span><b>' + escapeHtml(rows[i][1]) + '</b></div>';
  }
  return html;
}
function hexText(v) {
  return v ? String(v) : "—";
}
function analysisDetailsHtml(d, analysis) {
  var rows = [
    ["功率", analysis.power_watts ? String(analysis.power_watts) + " W" : "—"],
    ["心率", analysis.heart_rate_bpm ? String(analysis.heart_rate_bpm) : "—"],
    ["踏频", analysis.cadence_rpm ? String(analysis.cadence_rpm) + " rpm" : "—"],
    ["速度", analysis.speed_x10_kmh ? (analysis.speed_x10_kmh / 10).toFixed(1) + " km/h" : "—"],
    ["RSSI", d.last_rssi != null ? String(d.last_rssi) + " dBm" : "—"],
    ["样本", String(analysis.sample_count || 0)],
    ["服务", serviceSummary((d.service_flags || 0) | (analysis.service_flags || 0)).replace(/^服务：/, "")],
    ["控制", analysis.ftms_control_supported ? "支持" : "—"]
  ];
  var html = '<details class="analysis-details"><summary>详细数据</summary><div class="analysis-grid">';
  for (var i = 0; i < rows.length; i++) {
    html += '<div><span>' + escapeHtml(rows[i][0]) + '</span><b>' + escapeHtml(rows[i][1]) + '</b></div>';
  }
  html += '</div></details>';
  return html;
}
function analysisRawHtml(d, analysis) {
  var rows = [
    ["广播", d.adv_raw_hex],
    ["扫描响应", d.scan_rsp_raw_hex],
    ["厂商数据", d.manufacturer_data_hex],
    ["服务数据", d.service_data_hex],
    ["功率通知", analysis.last_cps_hex],
    ["心率通知", analysis.last_hrs_hex],
    ["踏频通知", analysis.last_csc_hex],
    ["骑行台通知", analysis.last_ftms_hex],
    ["骑行台能力", analysis.ftms_feature_hex]
  ];
  var html = '<details class="analysis-raw"><summary>原始数据</summary><div class="analysis-raw-list">';
  for (var i = 0; i < rows.length; i++) {
    html += '<div><span>' + escapeHtml(rows[i][0]) + '</span><code>' + escapeHtml(hexText(rows[i][1])) + '</code></div>';
  }
  html += '</div></details>';
  return html;
}
function analysisCardHtml(d) {
  if (!d || !d.addr || !STATE.template_analysis) return "";
  var analysis = STATE.template_analysis[d.addr];
  if (!analysis) return "";
  var metrics = [];
  if (analysis.power_watts) metrics.push("功率 " + analysis.power_watts + "W");
  if (analysis.cadence_rpm) metrics.push("踏频 " + analysis.cadence_rpm + "rpm");
  if (analysis.heart_rate_bpm) metrics.push("心率 " + analysis.heart_rate_bpm);
  if (analysis.speed_x10_kmh) metrics.push("速度 " + (analysis.speed_x10_kmh / 10).toFixed(1) + "km/h");
  var html = '<div class="analysis-card">';
  html += '<div class="analysis-head"><b>' + escapeHtml(analysis.status || "分析完成") + '</b><span>' + escapeHtml(String(analysis.sample_count || 0)) + ' 条样本</span></div>';
  html += '<div class="analysis-summary">' + escapeHtml(analysis.summary || "暂无运动数据") + '</div>';
  if (metrics.length) html += '<div class="analysis-metrics">' + escapeHtml(metrics.join(" · ")) + '</div>';
  html += analysisDetailsHtml(d, analysis);
  html += analysisRawHtml(d, analysis);
  html += '</div>';
  return html;
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
      html += analysisCardHtml(d);
      html += '<div class="template-actions"><button class="mini-btn primary" onclick="onTemplateReadDevice(' + s + ')">读取信息</button>';
      html += '<button class="mini-btn" onclick="onTemplateAnalyzeDevice(' + s + ')">分析数据</button></div>';
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
    var expanded = !!STATE.template_expanded[t.id];
    html += '<div class="template-item template-saved' + (expanded ? ' template-expanded' : '') + '">';
    html += '<button class="template-compact-head" onclick="onTemplateToggle(' + t.id + ')"><div class="template-title-row"><div class="template-title">' + escapeHtml(templateTitle(t)) + '</div><div class="svc-badges">' + serviceBadges(t.service_flags) + '</div></div><span class="chev">⌄</span></button>';
    if (expanded) {
      html += '<div class="template-sub">' + escapeHtml(templateSubtitle(t)) + '</div>';
      html += '<div class="template-detail-grid">' + templateDetailCells(t) + '</div>';
      html += '<div class="template-actions"><button class="mini-btn primary" onclick="onTemplateApply(' + t.id + ')">复制到本设备</button>';
      html += '<button class="mini-btn danger" onclick="onTemplateDelete(' + t.id + ')">' + (STATE.template_delete_arm === t.id ? '确认删除' : '删除') + '</button></div>';
    }
    html += '</div>';
  }
  box.innerHTML = html;
}
function onTemplateToggle(id) {
  STATE.template_expanded[id] = !STATE.template_expanded[id];
  renderTemplates();
}
function serviceSummary(flags) {
  var parts = [];
  flags = flags || 0;
  if (flags & 1) parts.push("设备信息");
  if (flags & 2) parts.push("电量");
  if (flags & 4) parts.push("功率");
  if (flags & 8) parts.push("心率");
  if (flags & 16) parts.push("踏频");
  if (flags & 32) parts.push("智能骑行台");
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
function onTemplateAnalyzeDevice(index) {
  var d = STATE.scan_devices[index];
  if (!d) return;
  toast("正在连接并分析数据…");
  api("/api/ble-scan/analyze", "POST", { addr: d.addr, addr_type: d.addr_type || 0 }, function (status, data) {
    if (status === 200 && data && data.ok && data.analysis) {
      STATE.template_analysis[d.addr] = data.analysis;
      if (data.template) {
        d.service_flags = (d.service_flags || 0) | (data.template.service_flags || 0);
        if (!d.device_name) d.device_name = data.template.device_name || "";
        if (!d.adv_name) d.adv_name = data.template.adv_name || "";
        if (!d.adv_raw_hex) d.adv_raw_hex = data.template.adv_raw_hex || "";
        if (!d.scan_rsp_raw_hex) d.scan_rsp_raw_hex = data.template.scan_rsp_raw_hex || "";
        if (!d.manufacturer_data_hex) d.manufacturer_data_hex = data.template.manufacturer_data_hex || "";
        if (!d.service_data_hex) d.service_data_hex = data.template.service_data_hex || "";
        if (!d.tx_power) d.tx_power = data.template.tx_power || 0;
        if (!d.appearance) d.appearance = data.template.appearance || 0;
      }
      renderTemplates();
      toast(data.analysis.status || "分析完成");
    } else {
      toast("分析失败: " + ((data && data.error) || status));
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
  var tpl = null;
  for (var i = 0; i < STATE.templates.length; i++) {
    if (STATE.templates[i].id === id) {
      tpl = STATE.templates[i];
      break;
    }
  }
  api("/api/device-templates/apply", "POST", { id: id }, function (status, data) {
    if (status === 200 && data && data.ok) {
      copyTemplateToState(tpl);
      renderAppliedTemplate();
      toast("模板设备信息已复制");
      api("/api/state", "GET", null, function (s, st) {
        if (s === 200 && st) {
          STATE.device_name = st.device_name || STATE.device_name;
          applyDeviceNameView();
          renderAppliedTemplate();
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
function isSettingsPanel(panel) {
  return panel === "profile" || panel === "device" || panel === "templates" || panel === "button" || panel === "led" || panel === "about";
}
function syncSettingsPanelFromRoute() {
  var hash = location.hash.replace(/^#/, "");
  if (hash.charAt(0) !== "/") hash = "/" + hash;
  var prefix = "/settings/";
  if (hash.indexOf(prefix) === 0) {
    var panel = hash.slice(prefix.length);
    if (isSettingsPanel(panel)) STATE.settings_panel = panel;
  } else if (hash === "/settings" && !isSettingsPanel(STATE.settings_panel)) {
    STATE.settings_panel = "profile";
  }
}
function settingsSubpageBack() {
  onSettingsPanel("device");
}
function onSettingsPanel(panel) {
  captureDraft();
  if (!isSettingsPanel(panel)) panel = "profile";
  STATE.settings_panel = panel;
  go(panel === "profile" ? "/settings" : ("/settings/" + panel));
}
function applySettingsPanel() {
  var active = STATE.settings_panel;
  var tabPanel = (active === "templates" || active === "button" || active === "led") ? "device" : active;
  var tabs = document.querySelectorAll(".settings-tab");
  for (var i = 0; i < tabs.length; i++) {
    var p = tabs[i].getAttribute("data-panel");
    tabs[i].classList.toggle("on", p === tabPanel);
  }
  var panels = document.querySelectorAll(".settings-panel");
  for (var j = 0; j < panels.length; j++) {
    panels[j].classList.toggle("on", panels[j].id === "settings-" + active);
  }
  var save = $("settings-save");
  var actions = document.querySelector(".settings-actions");
  if (actions) actions.style.display = active === "templates" ? "none" : "flex";
  if (save) save.style.display = active === "about" || active === "templates" ? "none" : "block";
  if (active === "led") syncLedSilentVisibility();
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
      STATE.active_sport_profile = STATE.sport_profile;
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
    sport_profile: STATE.sport_profile,
    mode: STATE.mode,
    power: STATE.power,
    cadence: STATE.cadence,
    heartrate: STATE.heartrate,
    run_speed_x100: STATE.run_speed_x100,
    run_pace_float_sec: STATE.run_pace_float_sec,
    run_cadence: STATE.run_cadence,
    run_cadence_float: STATE.run_cadence_float,
    run_mode: STATE.run_mode,
    run_pacer_type: STATE.run_pacer_type,
    run_pace_low_sec: STATE.run_pace_low_sec,
    run_pace_high_sec: STATE.run_pace_high_sec,
    run_custom_name: STATE.run_custom_name,
    run_incline_x10: STATE.run_incline_x10,
    hr_lock: directHrControl() ? 1 : (STATE.hr_locked ? 1 : 0),
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
      if (tip) tip.innerHTML = "WiFi 已关闭，设备已返回 BLE 模式";
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
      STATE.firmware_name = data.firmware_name || "";
      STATE.board = data.board || "";
      STATE.git_sha = data.git_sha || "";
      STATE.build_time = data.build_time || "";
      STATE.sport_profile = data.sport_profile || "cycling";
      STATE.active_sport_profile = STATE.sport_profile;
      STATE.broadcast_mask = data.broadcast_mask != null ? (data.broadcast_mask & 7) : 7;
      if (STATE.broadcast_mask === 0) STATE.broadcast_mask = 7;
      STATE.power = data.power || 0;
      STATE.cadence = data.cadence || 0;
      STATE.heartrate = data.heartrate || 0;
      STATE.run_speed_x100 = data.run_speed_x100 != null ? data.run_speed_x100 : 1000;
      STATE.run_pace_float_sec = data.run_pace_float_sec != null ? data.run_pace_float_sec : 10;
      STATE.run_cadence = data.run_cadence != null ? data.run_cadence : 170;
      STATE.run_cadence_float = data.run_cadence_float != null ? data.run_cadence_float : 4;
      STATE.run_mode = data.run_mode != null ? data.run_mode : 0;
      STATE.run_pacer_type = data.run_pacer_type != null ? data.run_pacer_type : 0;
      STATE.run_pace_low_sec = data.run_pace_low_sec != null ? data.run_pace_low_sec : 390;
      STATE.run_pace_high_sec = data.run_pace_high_sec != null ? data.run_pace_high_sec : 450;
      STATE.run_custom_name = data.run_custom_name || "自定义";
      STATE.run_incline_x10 = data.run_incline_x10 != null ? data.run_incline_x10 : 0;
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
      if (id === "home-power" || id === "home-hr" || id === "run-hr") updateZoneBadges();
    });
  }
  bindInputRange("home-power", 0, 2000);
  bindInputRange("home-cadence", 20, 120);
  bindInputRange("home-hr", 60, 200);
  bindInputRange("run-hr", 60, 200);
  var runPaceMin = $("run-pace-min");
  if (runPaceMin) runPaceMin.addEventListener("input", syncSpeedFromPace);
  var runPaceSec = $("run-pace-sec");
  if (runPaceSec) runPaceSec.addEventListener("input", syncSpeedFromPace);
  bindInputRange("run-cadence", 120, 220);
  bindInputRange("run-incline", -5, 15);
  bindInputRange("settings-weight", 40, 150);
  bindInputRange("settings-ftp", 50, 500);
  bindInputRange("settings-thr", 100, 220);
  var ledSilent = $("settings-led-silent");
  if (ledSilent) {
    ledSilent.addEventListener("change", function () {
      STATE.led_silent = ledSilent.checked ? 1 : 0;
      syncLedSilentVisibility();
    });
  }
}
if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", init);
} else {
  init();
}
