/* BikePower 配网门户前端逻辑（iOS Captive Portal WebView 兼容：无 eval/无动态 script/无 ES6 模块） */
"use strict";

/* ============ 全局状态 ============ */
var STATE = {
  device_name: "",
  device_name_draft: null,
  device_name_editing: false,
  firmware_version: "",
  power: 0, cadence: 0, heartrate: 0,
  mode: "steady",
  power_float: 5, cadence_float: 2, hr_float: 2,
  power_step: 20,
  profile_weight: 0, profile_ftp: 0, profile_thr_hr: 0, profile_ride_type: "coco",
  custom_wkg_low: 25, custom_wkg_high: 30,
  pacers: [],
  remaining_sec: 180,
  closing: false,
  hr_locked: false
};

/* 浮动幅度/步进范围（与 domain_config.h 一致；step 为单次加减量，默认 1） */
var FLOAT_RANGE = {
  power: { min: 0, max: 100, key: "power_float" },
  cadence: { min: 0, max: 20, key: "cadence_float" },
  hr: { min: 0, max: 20, key: "hr_float" },
  pstep: { min: 5, max: 50, key: "power_step", step: 5 }
};

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

/* ============ 路由 ============ */
var ROUTES = {
  "/home": renderHome,
  "/more": renderMore,
  "/about": renderAbout,
  "/closing": renderClosing,
  "/success": renderSuccess
};
function go(hash) {
  if (hash.charAt(0) === "#") hash = hash.slice(1);
  if (hash.charAt(0) !== "/") hash = "/" + hash;
  if (!ROUTES[hash]) hash = "/home";
  location.hash = hash;
  route();
}
function route() {
  captureDraft();
  var hash = location.hash.replace(/^#/, "");
  if (hash.charAt(0) !== "/") hash = "/" + hash;
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
  var more = $("view-more");
  if (more && more.classList.contains("active")) {
    STATE.profile_weight = parseInt($("more-weight").value, 10) || 0;
    STATE.profile_ftp = parseInt($("more-ftp").value, 10) || 0;
    STATE.profile_thr_hr = parseInt($("more-thr").value, 10) || 0;
    return;
  }
  var about = $("view-about");
  if (about && about.classList.contains("active")) {
    if (STATE.device_name_editing) {
      var inp = $("about-dev-input");
      if (inp) STATE.device_name_draft = inp.value;
    }
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
  return {
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
    custom_wkg_high: STATE.custom_wkg_high
  };
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

/* ============ 更多配置 ============ */
function renderMore() {
  $("more-weight").value = STATE.profile_weight;
  $("more-ftp").value = STATE.profile_ftp;
  $("more-thr").value = STATE.profile_thr_hr;
  var ps = $("float-pstep");
  if (ps) ps.textContent = STATE.power_step;
}
function onMoreSave() {
  var btn = $("more-save");
  btn.disabled = true;
  STATE.profile_weight = clamp(parseInt($("more-weight").value, 10) || 0, 40, 150);
  STATE.profile_ftp = clamp(parseInt($("more-ftp").value, 10) || 0, 50, 500);
  STATE.profile_thr_hr = clamp(parseInt($("more-thr").value, 10) || 0, 100, 220);
  var body = collectConfigBase();
  api("/api/config", "POST", body, function (status, data) {
    btn.disabled = false;
    if (status === 200 && data && data.ok) {
      toast("已保存");
      go("/home");
    } else {
      toast("保存失败: " + ((data && data.error) || status));
    }
  });
}
/* 更多页保存用：不依赖首页 DOM，取当前 STATE 全量字段 */
function collectConfigBase() {
  return {
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
    custom_wkg_high: STATE.custom_wkg_high
  };
}

/* ============ 关于 ============ */
function renderAbout() {
  $("about-ver").textContent = "v" + STATE.firmware_version;
  var dev = $("about-dev");
  var display = $("about-dev-wrap");
  var wrap = $("name-edit-wrap");
  var inp = $("about-dev-input");
  if (STATE.device_name_editing) {
    /* 编辑模式：显示输入框，隐藏铅笔 */
    if (dev) dev.textContent = STATE.device_name;
    if (display) display.style.display = "none";
    if (wrap) wrap.style.display = "flex";
    if (inp) {
      inp.value = STATE.device_name_draft != null ? STATE.device_name_draft : STATE.device_name;
    }
  } else {
    /* 显示模式：显示名称 + 铅笔，隐藏输入框 */
    if (dev) dev.textContent = STATE.device_name_draft != null ? STATE.device_name_draft : STATE.device_name;
    if (display) display.style.display = "flex";
    if (wrap) wrap.style.display = "none";
  }
  setupAboutPager();
}
function onEditDeviceName() {
  STATE.device_name_editing = true;
  var inp = $("about-dev-input");
  if (inp) inp.value = STATE.device_name_draft != null ? STATE.device_name_draft : STATE.device_name;
  var wrap = $("name-edit-wrap");
  if (wrap) wrap.style.display = "flex";
  var display = $("about-dev-wrap");
  if (display) display.style.display = "none";
  if (inp) {
    inp.focus();
    /* 选中现有名称便于修改 */
    try { inp.select(); } catch (e) { /* ignore */ }
  }
}
function onSaveDeviceName() {
  var inp = $("about-dev-input");
  if (!inp) return;
  var name = (inp.value || "").trim();
  if (!name) { toast("名称不能为空"); return; }
  if (name.length > 20) { toast("名称过长（最多 20 字符）"); return; }
  /* 保留当前其他配置，仅更新 device_name */
  var body = collectConfig();
  body.device_name = name;
  api("/api/config", "POST", body, function (status, data) {
    if (status === 200 && data && data.ok) {
      STATE.device_name = name;
      STATE.device_name_draft = null;
      STATE.device_name_editing = false;
      var dev = $("about-dev");
      if (dev) dev.textContent = name;
      var wrap = $("name-edit-wrap");
      if (wrap) wrap.style.display = "none";
      var display = $("about-dev-wrap");
      if (display) display.style.display = "flex";
      toast("设备名称已保存");
    } else {
      toast("保存失败: " + ((data && data.error) || status));
    }
  });
}

/* ============ 关于页分页滑动 ============ */
var s_pagerBound = false;
function setupAboutPager() {
  var pager = $("about-pager");
  if (!pager) return;
  var dotsBox = $("about-pager-dots");
  if (!dotsBox) return;
  var pages = pager.querySelectorAll(".page");
  /* 初始化小圆点（只在首次渲染时创建） */
  if (dotsBox.children.length !== pages.length) {
    dotsBox.innerHTML = "";
    for (var i = 0; i < pages.length; i++) {
      var d = el("div", { class: "pager-dot" + (i === 0 ? " on" : "") });
      d.setAttribute("data-idx", String(i));
      d.onclick = (function (idx) {
        return function () {
          pager.scrollTo({ left: idx * pager.clientWidth, behavior: "smooth" });
        };
      })(i);
      dotsBox.appendChild(d);
    }
  }
  /* 绑定滚动监听（只绑定一次） */
  if (!s_pagerBound) {
    s_pagerBound = true;
    pager.addEventListener("scroll", function () {
      var idx = Math.round(pager.scrollLeft / pager.clientWidth);
      var dots = dotsBox.querySelectorAll(".pager-dot");
      for (var j = 0; j < dots.length; j++) {
        dots[j].classList.toggle("on", j === idx);
      }
    }, { passive: true });
  }
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
      STATE.pacers = data.pacers || [];
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
      if (id === "home-power" || id === "home-hr") updateZoneBadges();
    });
  }
  bindInputRange("home-power", 0, 2000);
  bindInputRange("home-cadence", 20, 120);
  bindInputRange("home-hr", 60, 200);
  bindInputRange("more-weight", 40, 150);
  bindInputRange("more-ftp", 50, 500);
  bindInputRange("more-thr", 100, 220);
}
if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", init);
} else {
  init();
}
