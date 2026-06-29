/* BikePower 配网门户前端逻辑（iOS Captive Portal WebView 兼容：无 eval/无动态 script/无 ES6 模块） */
"use strict";

/* ============ 全局状态 ============ */
var STATE = {
  device_name: "",
  firmware_version: "",
  power: 0, cadence: 0, heartrate: 0,
  mode: "steady",
  profile_weight: 0, profile_ftp: 0, profile_thr_hr: 0, profile_ride_type: "coco",
  pacers: [],
  remaining_sec: 180,
  closing: false,
  home_wifi_ssid: ""
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
  "/ota": renderOta,
  "/wifi": renderWifi,
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
    return;
  }
  var more = $("view-more");
  if (more && more.classList.contains("active")) {
    STATE.profile_weight = parseInt($("more-weight").value, 10) || 0;
    STATE.profile_ftp = parseInt($("more-ftp").value, 10) || 0;
    STATE.profile_thr_hr = parseInt($("more-thr").value, 10) || 0;
    var sel = $("more-pacer");
    if (sel) STATE.profile_ride_type = sel.value;
  }
}

/* ============ 首页 ============ */
function renderHome() {
  $("home-ver").textContent = "v" + STATE.firmware_version;
  $("home-power").value = STATE.power;
  $("home-cadence").value = STATE.cadence;
  $("home-hr").value = STATE.heartrate;
  if (STATE.home_wifi_ssid) {
    $("home-wifi").style.display = "flex";
    $("home-wifi-val").textContent = STATE.home_wifi_ssid;
  } else {
    $("home-wifi").style.display = "none";
  }
  updateZoneBadges();
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
    profile_weight: STATE.profile_weight,
    profile_ftp: STATE.profile_ftp,
    profile_thr_hr: STATE.profile_thr_hr,
    profile_ride_type: STATE.profile_ride_type
  };
}
function onSave() {
  var btn = $("home-save");
  btn.disabled = true;
  btn.textContent = "保存中…";
  api("/api/config", "POST", collectConfig(), function (status, data) {
    btn.disabled = false;
    btn.textContent = "保存配置";
    if (status === 200 && data && data.ok) {
      toast("已保存");
    } else {
      toast("保存失败: " + ((data && data.error) || status));
    }
  });
}
function onCloseWifi() {
  if (!confirm("将保存当前配置并关闭 WiFi，确认？")) return;
  var btn = $("home-close");
  btn.disabled = true;
  btn.textContent = "保存中…";
  api("/api/close-wifi", "POST", collectConfig(), function (status, data) {
    btn.disabled = false;
    btn.textContent = "保存并关闭 WiFi";
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
  renderPacerSel();
}
function renderPacerSel() {
  var sel = $("more-pacer");
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
function onMoreSave() {
  var btn = $("more-save");
  btn.disabled = true;
  btn.textContent = "保存中…";
  var body = {
    device_name: STATE.device_name,
    mode: STATE.mode,
    power: STATE.power,
    cadence: STATE.cadence,
    heartrate: STATE.heartrate,
    profile_weight: clamp(parseInt($("more-weight").value, 10) || 0, 40, 150),
    profile_ftp: clamp(parseInt($("more-ftp").value, 10) || 0, 50, 500),
    profile_thr_hr: clamp(parseInt($("more-thr").value, 10) || 0, 100, 220),
    profile_ride_type: $("more-pacer").value
  };
  api("/api/config", "POST", body, function (status, data) {
    btn.disabled = false;
    btn.textContent = "保存配置";
    if (status === 200 && data && data.ok) {
      STATE.profile_weight = body.profile_weight;
      STATE.profile_ftp = body.profile_ftp;
      STATE.profile_thr_hr = body.profile_thr_hr;
      STATE.profile_ride_type = body.profile_ride_type;
      toast("已保存");
      go("/home");
    } else {
      toast("保存失败: " + ((data && data.error) || status));
    }
  });
}

/* ============ 关于 ============ */
function renderAbout() {
  $("about-ver").textContent = "v" + STATE.firmware_version;
  $("about-dev").textContent = STATE.device_name;
  apiText("/api/mem_stats", "GET", function (status, text) {
    if (status === 200 && text) {
      try {
        var d = JSON.parse(text);
        $("about-mem-free").textContent = (d.free / 1024).toFixed(1) + " KB";
        $("about-mem-min").textContent = (d.min_free / 1024).toFixed(1) + " KB";
      } catch (e) { /* ignore */ }
    }
  });
}

/* ============ OTA ============ */
var otaPollTimer = null;
function renderOta() {
  otaStopPoll();
  $("ota-status").innerHTML = '<div class="spin"></div>读取状态…';
  api("/api/ota/status", "GET", null, function (status, data) {
    if (status === 200 && data) {
      otaRenderStatus(data);
    } else {
      $("ota-status").innerHTML = '<div class="warn">无法读取 OTA 状态</div>';
    }
  });
}
function otaStopPoll() {
  if (otaPollTimer) { clearInterval(otaPollTimer); otaPollTimer = null; }
}
function otaRenderStatus(d) {
  var box = $("ota-status");
  var st = d.status || "idle";
  if (st === "started") st = "running";
  var html = "";
  html += '<div class="info-row"><span class="k">当前版本</span><span class="v">v' + escapeHtml(d.current || STATE.firmware_version) + '</span></div>';
  if (st === "running") {
    html += '<div class="info-row"><span class="k">检查中</span><span class="v"><span class="spinner"></span>查询服务器…</span></div>';
  } else if (st === "done") {
    html += '<div class="info-row"><span class="k">最新版本</span><span class="v">' + (d.has_update ? escapeHtml(d.version || "") : "已是最新") + '</span></div>';
    if (d.has_update) {
      html += '<div class="info-row"><span class="k">大小</span><span class="v">' + ((d.size || 0) / 1024).toFixed(0) + ' KB</span></div>';
      if (d.changelog) {
        html += '<div class="note" style="margin-top:8px">' + escapeHtml(d.changelog) + '</div>';
      }
      html += '<button class="btn green" id="ota-up-btn" onclick="otaStartUpgrade()" style="margin-top:10px">立即升级</button>';
    } else {
      html += '<div class="note" style="margin-top:8px">设备已是最新版本</div>';
    }
  } else if (st === "error") {
    html += '<div class="warn" style="margin-top:8px">检查失败：' + escapeHtml(d.error || "未知错误") + '</div>';
  } else {
    html += '<div class="note" style="margin-top:8px">尚未检查更新</div>';
  }
  box.innerHTML = html;
  var chk = $("ota-check-btn");
  if (chk) chk.disabled = (st === "running");
}
function otaCheck() {
  var btn = $("ota-check-btn");
  if (btn) { btn.disabled = true; btn.textContent = "检查中…"; }
  api("/api/ota/check", "POST", null, function (status, data) {
    if (btn) { btn.disabled = false; btn.textContent = "检查更新"; }
    if (status === 200 && data) {
      otaRenderStatus(data);
      if ((data.status === "running" || data.status === "started") && !otaPollTimer) {
        otaPollTimer = setInterval(otaPollStatus, 1500);
      }
    } else {
      $("ota-status").innerHTML = '<div class="warn">请求失败: ' + status + '</div>';
    }
  });
}
function otaPollStatus() {
  api("/api/ota/status", "GET", null, function (status, data) {
    if (status === 200 && data) {
      otaRenderStatus(data);
      if (data.status === "done" || data.status === "error") otaStopPoll();
    }
  });
}
function otaStartUpgrade() {
  if (!confirm("确认开始固件升级？升级期间请勿断电。")) return;
  var btn = $("ota-up-btn");
  if (btn) { btn.disabled = true; btn.textContent = "升级中…"; }
  api("/api/ota/upgrade", "POST", null, function (status, data) {
    if (status === 200 && data && data.ok) {
      $("ota-status").innerHTML = '<div class="result-card"><div class="icon info">⏳</div><h1>升级进行中</h1><div class="sub">设备将自动重启，请等待约 1 分钟后重新进入配网。</div></div>';
    } else {
      toast("升级失败: " + ((data && data.error) || status));
      if (btn) { btn.disabled = false; btn.textContent = "立即升级"; }
    }
  });
}

/* ============ WiFi 配网 ============ */
var wifiScanTimer = null;
var wifiConnTimer = null;
var wifiPickedSsid = "";
function renderWifi() {
  wifiStopScan();
  wifiStopConn();
  $("wifi-ssid").value = "";
  $("wifi-psk").value = "";
  $("wifi-scan-result").innerHTML = "";
  $("wifi-conn-status").innerHTML = "";
  if (STATE.home_wifi_ssid) {
    $("wifi-connected-box").style.display = "block";
    $("wifi-input-box").style.display = "none";
    $("wifi-connected-ssid").textContent = STATE.home_wifi_ssid;
  } else {
    $("wifi-connected-box").style.display = "none";
    $("wifi-input-box").style.display = "block";
  }
}
function wifiReconfig() {
  $("wifi-connected-box").style.display = "none";
  $("wifi-input-box").style.display = "block";
  $("wifi-ssid").value = STATE.home_wifi_ssid || "";
  $("wifi-psk").value = "";
  $("wifi-conn-status").innerHTML = "";
}
function wifiStartScan() {
  var btn = $("wifi-scan-btn");
  btn.disabled = true;
  btn.innerHTML = '<span class="spinner"></span>扫描中…';
  $("wifi-scan-result").innerHTML = "";
  api("/api/wifi/scan", "POST", null, function (status, data) {
    if (status === 200 && data && (data.status === "started" || data.status === "running")) {
      wifiScanTimer = setInterval(wifiPollScan, 1000);
    } else {
      btn.disabled = false;
      btn.textContent = "扫描附近 WiFi";
      $("wifi-scan-result").innerHTML = '<div class="warn">扫描未启动: ' + ((data && data.error) || status) + '</div>';
    }
  });
}
function wifiStopScan() {
  if (wifiScanTimer) { clearInterval(wifiScanTimer); wifiScanTimer = null; }
  var btn = $("wifi-scan-btn");
  if (btn) { btn.disabled = false; btn.textContent = "扫描附近 WiFi"; }
}
function wifiPollScan() {
  api("/api/wifi/scan/status", "GET", null, function (status, data) {
    if (status !== 200 || !data) return;
    if (data.status === "running") return;
    wifiStopScan();
    if (data.status === "done" && data.networks) {
      var box = $("wifi-scan-result");
      box.innerHTML = "";
      for (var i = 0; i < data.networks.length; i++) {
        var n = data.networks[i];
        var item = el("div", { class: "wifi-item" + (n.ssid === wifiPickedSsid ? " picked" : ""),
          onclick: (function (ssid) { return function () { wifiPick(ssid); }; })(n.ssid) },
          '<span style="font-weight:700">' + escapeHtml(n.ssid) + '</span>' +
          '<span style="font-size:12px;color:#86868b">' + (n.rssi || 0) + ' dBm</span>');
        box.appendChild(item);
      }
      if (data.networks.length === 0) {
        box.innerHTML = '<div class="note">未找到 WiFi 网络</div>';
      }
    } else {
      $("wifi-scan-result").innerHTML = '<div class="warn">扫描失败: ' + escapeHtml(data.error || "") + '</div>';
    }
  });
}
function wifiPick(ssid) {
  wifiPickedSsid = ssid;
  $("wifi-ssid").value = ssid;
  var items = document.querySelectorAll(".wifi-item");
  for (var i = 0; i < items.length; i++) {
    items[i].classList.toggle("picked", items[i].querySelector("span").textContent === ssid);
  }
}
function wifiSave() {
  var ssid = $("wifi-ssid").value.trim();
  var psk = $("wifi-psk").value;
  if (!ssid) { toast("请输入 SSID"); return; }
  var btn = $("wifi-save-btn");
  btn.disabled = true;
  btn.innerHTML = '<span class="spinner"></span>测试连接中…';
  $("wifi-conn-status").innerHTML = "";
  api("/api/wifi/save", "POST", { ssid: ssid, psk: psk }, function (status, data) {
    if (status === 200 && data && (data.status === "started" || data.status === "running")) {
      wifiConnTimer = setInterval(wifiPollConn, 1000);
    } else {
      btn.disabled = false;
      btn.textContent = "保存并测试连接";
      $("wifi-conn-status").innerHTML = '<div class="warn">连接测试未启动: ' + ((data && data.error) || status) + '</div>';
    }
  });
}
function wifiStopConn() {
  if (wifiConnTimer) { clearInterval(wifiConnTimer); wifiConnTimer = null; }
  var btn = $("wifi-save-btn");
  if (btn) { btn.disabled = false; btn.textContent = "保存并测试连接"; }
}
function wifiPollConn() {
  api("/api/wifi/connect/status", "GET", null, function (status, data) {
    if (status !== 200 || !data) return;
    if (data.status === "running") return;
    wifiStopConn();
    var box = $("wifi-conn-status");
    if (data.status === "done" && data.connected) {
      box.innerHTML = '<div class="result-card"><div class="icon ok">✓</div><h1>连接成功</h1><div class="sub">已连接到 ' + escapeHtml(data.ssid || "") + '，IP: ' + escapeHtml(data.ip || "") + '</div></div>';
      STATE.home_wifi_ssid = data.ssid || "";
    } else {
      box.innerHTML = '<div class="result-card"><div class="icon warn">!</div><h1>连接失败</h1><div class="sub">' + escapeHtml(data.error || "请检查 SSID 和密码") + '</div></div>';
    }
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
      STATE.profile_weight = data.profile_weight || 70;
      STATE.profile_ftp = data.profile_ftp || 200;
      STATE.profile_thr_hr = data.profile_thr_hr || 180;
      STATE.profile_ride_type = data.profile_ride_type || "coco";
      STATE.pacers = data.pacers || [];
      STATE.remaining_sec = data.remaining_sec != null ? data.remaining_sec : 180;
      STATE.closing = !!data.closing;
      STATE.home_wifi_ssid = data.home_wifi_ssid || "";
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
