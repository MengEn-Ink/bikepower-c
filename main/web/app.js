(function () {
  "use strict";
  var $ = function(id) { return document.getElementById(id); };
  function text(id, value) { $(id).textContent = value; }
  var themeKey = "bikepower.portal.theme.v1", themeChoice = "";
  var themeMedia = window.matchMedia("(prefers-color-scheme: dark)");
  try { themeChoice = localStorage.getItem(themeKey); } catch (_) {}
  if (themeChoice !== "light" && themeChoice !== "dark") themeChoice = "";
  function applyTheme() {
    var theme = themeChoice || (themeMedia.matches ? "dark":"light");
    document.documentElement.dataset.theme = theme;
    var label = theme === "dark" ? "切换为浅色主题":"切换为深色主题";
    $("themeToggle").setAttribute("aria-label",label);
    $("themeToggle").title = label;
    document.querySelector('meta[name="theme-color"]').content = theme === "dark" ? "#14161a":"#f3f4f6";
  }
  $("themeToggle").addEventListener("click",function() {
    themeChoice = document.documentElement.dataset.theme === "dark" ? "light":"dark";
    try { localStorage.setItem(themeKey,themeChoice); } catch (_) {}
    applyTheme();
  });
  if (themeMedia.addEventListener) themeMedia.addEventListener("change",applyTheme);
  else if (themeMedia.addListener) themeMedia.addListener(applyTheme);
  applyTheme();
  var clone = function(value) { return JSON.parse(JSON.stringify(value)); };
  var devices = {};
  var state = {loaded:false, busy:false, message:"正在读取设备…", applied:null, draft:null,
    wire:null, revision:0, runtime:"normal", rawPower:0, wifiDeadline:0,
    bridgeStage:"idle", bridgeHeartInitialized:false, copyIdentity:false,
    nameSource:"board", customName:"", toastTimer:0, pacers:[], runPacers:[],
    bridge:null, bridgeRuntime:null, output:null, testCandidate:null,
    pendingReplacementKey:null, replaceSlot:null, polling:false, closed:false,
    closeRequested:false, closeConfirmed:false, leaseVersion:0, idleSeconds:300,
    renewing:false, activityTimer:0, activityPending:false, lastRenewAt:0,
    pointerActive:false};
  var ACTIVITY_MERGE_MS = 1000, CLOSE_WARNING_SEC = 30;
  var pollTimer = 0, memoryTimer = 0;
  var emptyIdentity = {manufacturer:"",model:"",serial:"",hardware:"",firmware:"",software:""};
  function deviceKey(address, type) { return address + ":" + type; }
  function rememberDevice(item, info) {
    var key = deviceKey(item.address,item.addr_type);
    devices[key] = Object.assign(devices[key] || {}, {
      name:item.device_name || item.name || "未命名功率计",
      named:!!(item.device_name || item.name),
      suffix:(item.address || "").match(/.{2}/g)?.reverse().join(":") || "—",
      cadence:item.cadence > 0, mark:"PM", info:info || item.identity || clone(emptyIdentity),
      address:item.address,addrType:item.addr_type
    });
    return key;
  }
  function rememberBridge(b) {
    return rememberDevice(b,b.identity || clone(emptyIdentity));
  }
  function decode(c) {
    var total = Math.round(360000 / c.run_speed_x100);
    var saved = Array.isArray(c.bridge_devices) ? c.bridge_devices : [];
    if (!saved.length && c.bridge_bound) saved = [{
      address:c.bridge_address,addr_type:c.bridge_addr_type,name:c.bridge_name
    }];
    var keys = saved.map(function(item) {
      return rememberDevice(item,clone(emptyIdentity));
    });
    var key = keys[0] || rememberDevice({
      address:"000000000000",addr_type:0,name:"未选择功率计"
    },clone(emptyIdentity));
    return {sport:c.sport_profile ? "running":"cycling", source:c.cycling_source ? "bridge":"simulation",
      device:key,devices:keys,
      calibration:{mode:["raw","watts","percent"][c.bridge_calibration],watts:c.bridge_watts,percent:c.bridge_percent_x10 / 10 - 100},
      simulation:{mode:c.mode === 1 ? "pacer":"fixed",power:c.power,cadence:c.cadence,weight:c.weight,
        preset:c.ride_type === 5 ? "custom":String(c.ride_type),wkgMin:c.custom_wkg_low / 10,
        wkgMax:c.custom_wkg_high / 10,customName:c.custom_pacer_name,powerFloat:c.power_float,cadenceFloat:c.cadence_float},
      broadcast:{power:!!(c.broadcast_mask & 1),cadence:!!(c.broadcast_mask & 2),heart:!!(c.broadcast_mask & 4),cadenceWithPower:!!(c.broadcast_mask & 8)},
      heart:{enabled:!!(c.broadcast_mask & 4),mode:c.has_user_hr ? "fixed":"linked",ftp:c.ftp,
        threshold:c.thr_hr,fixed:c.heartrate,float:c.hr_float},
      running:{mode:c.run_mode ? "pacer":"fixed",preset:c.run_pacer_type === 4 ? "custom":String(c.run_pacer_type),
        low:c.run_pace_low_sec,high:c.run_pace_high_sec,customName:c.run_custom_name,
        paceMinutes:Math.floor(total / 60),paceSeconds:total % 60,cadence:c.run_cadence,
        cadenceFloat:c.run_cadence_float,paceFloat:c.run_pace_float_sec},
      deviceName:c.device_name,identity:clone(emptyIdentity),powerStep:c.power_step};
  }
  function encode(config) {
    var c = clone(state.wire), s = config.simulation, h = config.heart, r = config.running;
    var saved = config.devices.map(function(key) {
      var item = devices[key];
      return {addr_type:item.addrType,address:item.address,name:item.named ? item.name:""};
    });
    var meter = devices[config.devices[0]] || devices[config.device];
    Object.assign(c, {sport_profile:config.sport === "running" ? 1:0,
      cycling_source:config.source === "bridge" ? 1:0,
      bridge_devices:saved,bridge_bound:saved.length ? 1:0,bridge_address:saved.length ? meter.address:"000000000000",
      bridge_addr_type:meter.addrType,bridge_name:saved.length ? (meter.named ? meter.name:""):c.bridge_name,
      bridge_calibration:["raw","watts","percent"].indexOf(config.calibration.mode),
      bridge_watts:config.calibration.watts,bridge_percent_x10:Math.round((100 + config.calibration.percent) * 10),
      mode:s.mode === "pacer" ? 1:0,power:s.power,cadence:s.cadence,weight:s.weight,
      ride_type:s.preset === "custom" ? 5:Number(s.preset),custom_wkg_low:Math.round(s.wkgMin * 10),
      custom_wkg_high:Math.round(s.wkgMax * 10),custom_pacer_name:s.customName,
      power_float:s.powerFloat,cadence_float:s.cadenceFloat,has_user_hr:h.mode === "fixed" ? 1:0,
      heartrate:h.fixed,ftp:h.ftp,thr_hr:h.threshold,hr_float:h.float,
      broadcast_mask:(config.broadcast.power ? 1:0) | (config.broadcast.cadence ? 2:0) | (h.enabled ? 4:0) | (config.broadcast.cadenceWithPower ? 8:0),
      run_speed_x100:Math.round(360000 / (r.paceMinutes * 60 + r.paceSeconds)),
      run_mode:r.mode === "fixed" ? 0:1,run_pacer_type:r.preset === "custom" ? 4:Number(r.preset),
      run_pace_low_sec:r.low,run_pace_high_sec:r.high,run_custom_name:r.customName,
      run_pace_float_sec:r.paceFloat,run_cadence:r.cadence,run_cadence_float:r.cadenceFloat,
      device_name:config.deviceName,power_step:config.powerStep});
    return c;
  }
  function pendingConfig() {
    var c = clone(state.draft), d = devices[c.device] || devices[c.devices[0]];
    if (state.copyIdentity && c.source === "bridge") {
      Object.keys(d.info).forEach(function(k) { if (d.info[k]) c.identity[k] = d.info[k]; });
      if (state.nameSource === "meter" && d.named) c.deviceName = d.name;
      if (state.nameSource === "custom") c.deviceName = state.customName.trim();
    }
    return c;
  }
  function isDirty() { return state.loaded && (state.copyIdentity || JSON.stringify(encode(pendingConfig())) !== JSON.stringify(state.wire)); }
  function bridgeReady() {
    return state.draft && state.draft.devices.length > 0;
  }
  function testedBridgeReady() {
    return state.bridge && state.bridge.temporary && state.bridge.tested
      && deviceKey(state.bridge.address,state.bridge.addr_type) === state.draft.device;
  }
  function textValid(value, limit) {
    return typeof value === "string" && new TextEncoder().encode(value).length <= limit
      && !/[\u0000-\u001f\u007f]/.test(value);
  }
  function validFields() {
    return !!state.draft && !validationErrors().length;
  }
  async function api(path, body) {
    var controller = new AbortController(), timeout = setTimeout(function() { controller.abort(); },7000);
    try {
      var response = await fetch("/api/" + path,{method:body === undefined ? "GET":"POST",
        cache:"no-store",headers:body === undefined ? {}:{"Content-Type":"application/json"},
        body:body === undefined ? undefined:JSON.stringify(body),signal:controller.signal});
      if (!response.ok) throw new Error("设备请求失败：" + response.status);
      var data;
      if (path === "renew") {
        var text = await response.text();
        data = text.trim().charAt(0) === "{" ? JSON.parse(text):{remaining_sec:Number(text)};
        if (data.closing) endSession(true,data.remaining_sec || 0);
        if (data.ok !== false && (!Number.isFinite(data.remaining_sec) || data.remaining_sec <= 0)) throw new Error("续期响应无效");
      } else data = await response.json();
      if (data.ok === false) throw new Error(data.error || "设备拒绝操作");
      return data;
    } catch (e) {
      if (body !== undefined && (!response || (response.ok && (!data || data.ok !== false)))) e.resultUnknown = true;
      throw e;
    } finally { clearTimeout(timeout); }
  }
  function setBusy(value) {
    state.busy = value;
    document.querySelector(".editor-pane").classList.toggle("is-busy",value);
    document.querySelector(".editor-pane").setAttribute("aria-busy",String(value));
    $("saveBtn").setAttribute("aria-busy",String(value));
    $("closeWifiBtn").disabled = value;
    ["profileWeight","ftpValue","thresholdHeart"].forEach(function(id) { $(id).disabled = value || state.closed; });
    if (state.loaded) renderFloatInputs();
    if (state.loaded) updateDirty();
  }
  function countdown(seconds, version, startedAt) {
    if (state.closed || (version !== undefined && version !== state.leaseVersion)) return;
    if (!Number.isFinite(seconds) || seconds < 0) return;
    state.wifiDeadline = (startedAt === undefined ? performance.now():startedAt) + seconds * 1000;
    renderTimer();
  }
  function endSession(confirmed, seconds) {
    if (state.closeConfirmed && !confirmed) return;
    state.closed = true; state.closeConfirmed = confirmed;
    state.wifiDeadline = performance.now() + Math.max(0,seconds || 0) * 1000;
    clearTimeout(state.activityTimer); state.activityTimer = 0; state.activityPending = false;
    clearInterval(pollTimer); clearInterval(memoryTimer);
    ["scanModal","closeModal"].forEach(function(id) { $(id).hidden = true; });
    document.querySelector(".page").inert = true;
    document.querySelector(".action-bar").inert = true;
    $("profileBar").inert = true;
    if (state.loaded) renderFloatInputs();
    $("restoreConfigBtn").disabled = $("locateErrorBtn").disabled = true;
    $("renewBtn").disabled = true;
    $("sessionWarning").hidden = true;
    $("wifiClosed").hidden = false;
    $("wifiClosed").dataset.confirmed = String(confirmed);
    text("wifiClosedTitle",confirmed ? "本次配网已结束":"配网时间已到");
    text("wifiClosedState",confirmed ? "正在关闭 WiFi":"设备状态待确认");
    $("wifiClosedText").textContent = confirmed
      ? "设备已接受关闭，WiFi 正在退出；已应用的运动输出继续运行。"
      : "当前无法确认设备是否已关闭。请检查热点连接；不要把网络中断当成保存成功。";
    $("sessionRetryBtn").hidden = confirmed;
    $("wifiClosed").focus();
  }
  async function renewSession(manual) {
    if (!state.loaded || state.closed || state.closeRequested || state.renewing) return;
    state.renewing = true; state.activityPending = false;
    state.lastRenewAt = performance.now();
    $("renewBtn").disabled = true;
    try {
      var data = await api("renew",{});
      if (state.closed || state.closeRequested) return;
      state.leaseVersion++;
      countdown(Math.max(0,data.remaining_sec - (performance.now() - state.lastRenewAt) / 1000));
      $("sessionRenewError").hidden = true;
      if (manual) showToast("已续期，闲置 5 分钟后关闭");
    } catch(e) {
      if (!state.closed) {
        $("sessionRenewError").hidden = false;
        text("sessionRenewError","续期未确认，请检查热点连接；倒计时未延长。");
        if (manual) showToast(e.message);
      }
    } finally {
      state.renewing = false;
      $("renewBtn").disabled = state.closed || state.closeRequested;
      if (state.activityPending && !state.closed && !state.closeRequested) {
        clearTimeout(state.activityTimer);
        state.activityTimer = setTimeout(function() { state.activityTimer = 0; renewSession(false); },ACTIVITY_MERGE_MS);
      }
    }
  }
  function userActivity(event) {
    if (!event.isTrusted || document.hidden || !state.loaded || state.closed || state.closeRequested) return;
    if (event.target.closest && event.target.closest("#themeToggle,#renewBtn,#warningRenewBtn,#confirmCloseBtn,#wifiClosed")) return;
    if (event.type === "pointermove" && (!state.pointerActive || !event.buttons)) return;
    state.activityPending = true;
    if (state.renewing || state.activityTimer) return;
    var delay = Math.max(0,ACTIVITY_MERGE_MS - (performance.now() - state.lastRenewAt));
    if (!delay) renewSession(false);
    else state.activityTimer = setTimeout(function() { state.activityTimer = 0; renewSession(false); },delay);
  }
  document.addEventListener("pointerdown",function(e) { if (e.isTrusted) state.pointerActive = true; },true);
  ["pointerup","pointercancel"].forEach(function(type) {
    document.addEventListener(type,function() { state.pointerActive = false; },true);
  });
  ["pointerdown","pointermove","click","input","change","keydown","wheel","touchmove"].forEach(function(type) {
    document.addEventListener(type,userActivity,{capture:true,passive:true});
  });
  function fillPresets() {
    $("robotStops").replaceChildren();
    state.pacers.concat({id:5,name:"自定义"}).forEach(function(p) {
      var button = document.createElement("button");
      button.type = "button"; button.dataset.ride = p.id === 5 ? "custom":String(p.id);
      button.textContent = p.id === 5 ? "自定义":p.name;
      button.addEventListener("click",function() { chooseRobot(this.dataset.ride); });
      $("robotStops").appendChild(button);
    });
    [["runPreset",state.runPacers,4]].forEach(function(entry) {
      var select = $(entry[0]); select.replaceChildren();
      entry[1].filter(function(p) { return p.id !== entry[2]; }).forEach(function(p) {
        var option = document.createElement("option");
        option.value = String(p.id);
        option.textContent = p.name;
        select.appendChild(option);
      });
      var custom = document.createElement("option"); custom.value = "custom";
      custom.textContent = "自定义"; select.appendChild(custom);
    });
  }
  async function load(preserve, snapshot) {
    var version = state.leaseVersion, startedAt = performance.now(), firstLoad = !state.loaded;
    var data = snapshot || await api("state");
    if (state.closed) return data;
    state.wire = clone(data.config); state.revision = data.revision;
    state.pacers = data.pacers; state.runPacers = data.run_pacers; state.bridge = data.bridge;
    rememberBridge(data.bridge);
    state.applied = decode(data.config);
    state.applied.identity = data.identity || clone(emptyIdentity);
    if (!preserve || !state.draft) {
      resetDraft();
    }
    state.loaded = true; state.message = "";
    state.idleSeconds = data.idle_timeout_sec || state.idleSeconds;
    if (data.closing) { endSession(true,data.remaining_sec); return data; }
    countdown(data.remaining_sec,version,startedAt);
    fillPresets();
    [["firmwareVersion",data.firmware_version],["boardType",data.board],
      ["gitSha",data.git_sha],["buildTime",data.build_time]].forEach(function(x) { $(x[0]).textContent = x[1]; });
    text("schemaVersion","schema v" + data.config.schema_ver);
    text("headerFirmware","固件 v" + data.firmware_version);
    if (!preserve) renderAll(); else updateDirty();
    if (firstLoad) {
      $("sourceOptions").open = state.draft.source === "bridge" || state.draft.simulation.mode === "pacer";
      $("heartOptions").open = state.draft.heart.mode === "fixed";
      $("broadcastOptions").open = (data.config.broadcast_mask & 7) !== 7;
    }
    return data;
  }
  function renderLive() {
    if (!state.output) return;
    var o = state.output, c = state.applied, b = o.bridge;
    var runtime = o.bridge_runtime || {state:"disabled",actual_source:c.source === "bridge" ? "real":"simulation",active_slot:0,next_attempt_ms:0};
    var running = c.sport === "running", bridge = !running && c.source === "bridge";
    var active = bridge && runtime.actual_source === "real" && !b.temporary && b.valid;
    var fallback = bridge && runtime.actual_source === "simulation";
    var valid = !bridge || active || fallback;
    var labels = ["未启动","连接中","读取服务","订阅中","等待功率","数据有效","数据过期","重连中","停止中","连接失败"];
    text("liveTitle",running ? "跑步广播":fallback ? "桥接模拟降级":bridge ? "真实功率桥接":"模拟骑行输出");
    text("liveSubtitle","已应用配置 · App 连接 " + o.connections);
    text("heroMetricLabel",running ? "当前速度":bridge ? "校正后功率":"模拟功率");
    text("heroMetricValue",running ? (o.run_speed_x100 / 100).toFixed(2):o.power);
    text("heroMetricUnit",running ? "km/h":"W");
    text("heroMetricMeta",running ? "累计 " + o.run_distance_m + " m · " + o.run_elapsed_sec + " 秒":bridge && !fallback ? "原始输入 " + (active ? b.raw_power:"—") + " W":"目标 " + c.simulation.power + " W");
    text("metricTwoLabel",running ? "步频":bridge ? "真实踏频":"模拟踏频");
    text("metricTwoValue",o.cadence);
    text("metricTwoUnit",running ? "spm":"rpm");
    text("metricTwoMeta",bridge ? "同一功率计，未提供时为零":"本机平滑生成");
    text("heartLiveValue",c.heart.enabled ? o.heartrate:"—");
    text("heartLiveMeta",c.heart.enabled ? "模拟心率":"已关闭");
    document.querySelector(".signal-chain").hidden = !bridge;
    text("chainSource",fallback ? "模拟引擎":(devices[deviceKey(b.address,b.addr_type)] || devices[c.device]).name);
    text("chainSourceState",fallback ? "维持完整模拟输出":active ? labels[b.state]:"等待已保存功率计");
    text("chainTransform",calibrationText(c));
    $("liveStatus").className = "status-chip " + (valid ? "status-chip--ok":"status-chip--warn");
    text("liveStatus",running ? "跑步广播中":fallback ? "模拟降级":bridge ? (active ? labels[b.state]:"上游未连接"):"模拟运行");
    $("runtimeAlert").className = "runtime-alert" + (valid ? "":" is-warn");
    text("runtimeAlertTitle",running ? "跑步模拟正在运行":fallback ? "真实来源不可用，模拟输出继续":bridge ? (valid ? "桥接正在运行":"正在恢复真实功率计"):"模拟输出正在运行");
    text("runtimeAlertText",running ? "速度与步频由设备本机平滑生成。":fallback ? "设备会按低频节拍自动尝试已保存功率计。":bridge ? (valid ? "功率与踏频来自同一台功率计。":"快速恢复期间等待真实数据。"):"输出由设备本机平滑生成。");
    text("packetCount",b.valid ? (b.age_ms / 1000).toFixed(1) + " 秒":"—");
    text("eventStateLabel",running ? "步频状态":"曲柄事件");
    text("crankState",running ? (o.cadence > 0 ? "步频有效":"已停止"):(b.cadence > 0 ? "踏频有效":"未提供或已停止"));
    text("appServices",running ? "RSC / HRS / FTMS":"CPS / CSC / FTMS");
    text("upstreamStateLabel",running ? "会话来源":"上游状态");
    text("reconnectState",running ? "本机模拟":labels[b.state] || "未知");
    renderBridgeRuntime();
  }

  function renderBridgeRuntime() {
    if (!state.draft || !state.applied) return;
    var runtime = state.bridgeRuntime || {
      state:"disabled", configured_real:state.applied.source === "bridge",
      actual_source:state.applied.source === "bridge" ? "real":"simulation",
      active_slot:-1, candidate_slot:-1, next_attempt_ms:0
    };
    text("configuredSource",state.applied.source === "bridge" ? "真实功率计":"模拟输出");
    var activeKey = runtime.active_slot >= 0 ? state.applied.devices[runtime.active_slot] : null;
    var activeDevice = activeKey ? devices[activeKey] : null;
    $("actualSource").textContent = runtime.state === "fast_recovery"
      ? "快速恢复中"
      : runtime.actual_source === "simulation" && runtime.configured_real
        ? "模拟降级"
        : activeDevice ? "真实功率计 · " + activeDevice.name:"模拟引擎";
    var seconds = Math.ceil((runtime.next_attempt_ms || 0) / 1000);
    text("nextBridgeAttempt",seconds > 0 ? seconds + " 秒":"—");
    $("retryBridgeBtn").hidden = !runtime.configured_real;
    var fallback = runtime.actual_source === "simulation" && runtime.configured_real;
    $("fallbackSimulationSummary").hidden = !fallback;
    $("fallbackSimulationSummary").textContent = fallback
      ? "降级输出：模拟 " + state.applied.simulation.power + " W / "
        + state.applied.simulation.cadence + " rpm":"";
  }
  function setActive(selector, value, key) {
    document.querySelectorAll(selector).forEach(function (button) {
      var active = button.dataset[key] === value;
      button.classList.toggle("is-active", active);
      button.setAttribute("aria-pressed", String(active));
    });
  }

  function sportModePending() {
    return !!state.draft && !!state.applied && state.draft.sport !== state.applied.sport;
  }

  function outputPower(config) {
    if (config.source === "simulation") {
      if (config.simulation.mode === "fixed") return config.simulation.power;
      var range = robotRange(config.simulation);
      return Math.round(config.simulation.weight * (range[0] + range[1]) / 2);
    }
    if (state.rawPower <= 0) return 0;
    if (state.runtime !== "normal"
        && !(state.bridge && state.bridge.temporary && state.bridge.tested
          && state.bridge.valid)) return 0;
    if (config.calibration.mode === "watts") return Math.max(0, Math.min(2000, Math.round(state.rawPower + config.calibration.watts)));
    if (config.calibration.mode === "percent") return Math.max(0, Math.min(2000, Math.round(state.rawPower * (1 + config.calibration.percent / 100))));
    return state.rawPower;
  }

  function calibrationText(config) {
    if (config.calibration.mode === "watts") return (config.calibration.watts > 0 ? "+" : "") + config.calibration.watts + " W";
    if (config.calibration.mode === "percent") return (config.calibration.percent > 0 ? "+" : "") + config.calibration.percent + "%";
    return "原值";
  }

  function showToast(message) {
    clearTimeout(state.toastTimer);
    text("toast",message);
    $("toast").classList.add("is-visible");
    state.toastTimer = setTimeout(function () { $("toast").classList.remove("is-visible"); }, 2200);
  }

  function updateDirty() {
    renderDisclosure();
    var dirty = isDirty(), errors = validationErrors(), saveBtn = $("saveBtn"), hint = $("saveHint");
    var bridgePending = state.draft.sport === "cycling"
      && state.draft.source === "bridge"
      && !bridgeReady();
    $("draftChip").hidden = !dirty;
    $("restoreConfigBtn").hidden = !dirty;
    saveBtn.disabled = !state.loaded || state.busy || !dirty || bridgePending || errors.length > 0;
    $("restoreConfigBtn").disabled = !dirty || state.busy || state.closed;
    $("locateErrorBtn").hidden = !errors.length;
    $("locateErrorBtn").disabled = state.busy || state.closed;
    text("saveState",state.busy ? "正在确认设备保存结果…" : errors.length ? errors[0][1] : state.message || (dirty ? "有未保存修改" : "当前配置已应用"));
    hint.textContent = bridgePending
      ? "先选择并测试一台功率计"
      : (dirty ? "实际输出继续使用已应用配置" : "关闭网页不影响当前输出");
    saveBtn.textContent = bridgePending
      ? "选择功率计后继续"
      : (state.draft.source === "bridge" && state.draft.sport === "cycling" ? "保存并启用桥接" : "保存并应用");
    if (sportModePending()) {
      saveBtn.textContent = "保存并切换到" + (state.draft.sport === "running" ? "跑步":"骑行");
      hint.textContent = state.busy ? "正在切换运动模式…":"保存后 BLE 服务将短暂重建";
    }
    if (errors.length) hint.textContent = "定位后修正；其他草稿与模式保持";
    if (state.busy) saveBtn.textContent = state.closeRequested ? "正在关闭 WiFi…":"正在保存…";
  }

  function renderHeader() {
    text("headerDeviceName",state.applied.deviceName);
    $("deviceNameInput").value = state.draft.deviceName;
    $("powerStepInput").value = state.draft.powerStep;
    ["profileWeight","ftpValue","thresholdHeart"].forEach(function(id) { $(id).disabled = state.busy || state.closed; });
  }

  function renderSport() {
    var pending = sportModePending();
    setActive("[data-sport]", state.draft.sport, "sport");
    $("cyclingEditor").hidden = state.draft.sport !== "cycling";
    $("runningEditor").hidden = state.draft.sport !== "running";
    text("editorTitle",state.draft.sport === "cycling" ? "骑行输出" : "跑步输出");
    document.querySelector(".workspace").classList.toggle("is-mode-pending", pending);
    document.querySelector(".live-pane").hidden = pending;
    document.querySelector(".editor-pane").style.maxWidth = pending ? "760px":"";
    document.querySelector(".editor-pane").style.margin = pending ? "auto":"";
    $("modeSwitchNotice").hidden = !pending;
    text("modeSwitchTitle",state.draft.sport === "running" ? "跑步模式尚未应用":"骑行模式尚未应用");
    $("modeSwitchText").textContent = "保存后设备将重建" + (state.draft.sport === "running" ? "跑步":"骑行")
      + "广播，当前 BLE 连接可能短暂断开。";
  }

  function renderSource() {
    var bridgeSelected = state.draft.source === "bridge";
    var ready = bridgeSelected && bridgeReady();
    setActive("[data-source]", state.draft.source, "source");
    $("bridgeIntro").hidden = !bridgeSelected || ready;
    $("bridgeEditor").hidden = !ready;
    $("simulationEditor").hidden = state.draft.source !== "simulation";
    $("bridgeHeartHint").hidden = !ready;
    $("selectedDeviceDetail").hidden = !ready;
    $("stopBridgeBtn").hidden = state.applied.source !== "bridge" || state.applied.sport !== "cycling";
    $("sourceSummary").textContent = bridgeSelected
      ? (state.applied.source === "bridge" && !isDirty()
        ? "真实功率 + 同源踏频"
        : (ready ? "功率计已测试，等待保存" : "尚未选择功率计"))
      : "本机平滑模拟";
    text("broadcastIndex",bridgeSelected ? (ready ? "04" : "03") : "03");
    $("heartIndex").textContent = state.draft.sport === "running" ? "02"
      : bridgeSelected ? (ready ? "05" : "04") : "04";
    var powerCheck = document.querySelector('[data-broadcast="power"]');
    powerCheck.disabled = state.draft.source === "bridge";
    if (powerCheck.disabled) {
      powerCheck.checked = true;
      state.draft.broadcast.power = true;
    }
    document.querySelectorAll("[data-broadcast]").forEach(function (input) {
      input.checked = state.draft.broadcast[input.dataset.broadcast];
    });
    var bundleCheck = document.querySelector("[data-cadence-bundle]");
    if (bundleCheck) {
      bundleCheck.checked = !!state.draft.broadcast.cadenceWithPower;
      bundleCheck.disabled = state.draft.sport === "running";
    }
    renderBridgeDevices();
    renderBridgeRuntime();
  }

  function renderBridgeDevices() {
    var list = $("bridgeDeviceList");
    list.replaceChildren();
    state.draft.devices.forEach(function(key,index) {
      var item = devices[key], fragment = $("bridgeDeviceTemplate").content.cloneNode(true);
      var row = fragment.querySelector("[data-device]");
      row.dataset.device = key;
      row.querySelector(".bridge-device-name b").textContent = item.name;
      row.querySelector(".bridge-device-name span").textContent =
        item.suffix + " · " + (index === 0 ? "首选":"备选 " + (index + 1));
      var runtime = state.bridgeRuntime || {};
      var activeKey = runtime.active_slot >= 0 ? state.applied.devices[runtime.active_slot] : null;
      row.querySelector(".bridge-device-state").textContent = activeKey === key
        && runtime.actual_source === "real" ? "当前使用":index === 0 ? "首选":"待命";
      var prefer = row.querySelector('[data-action="prefer"]');
      prefer.disabled = index === 0;
      prefer.addEventListener("click",function() {
        state.draft.devices.splice(index,1);
        state.draft.devices.unshift(key);
        state.draft.device = key;
        renderAll(); edited();
      });
      row.querySelector('[data-action="delete"]').addEventListener("click",function() {
        if (!window.confirm("删除已保存功率计“" + item.name + "”？")) return;
        state.draft.devices.splice(index,1);
        state.draft.device = state.draft.devices[0] || deviceKey("000000000000",0);
        state.copyIdentity = false;
        renderAll(); edited();
      });
      list.appendChild(fragment);
    });
  }

  function fillInfo(id, info, name, missing) {
    var fields = { name: "设备名称", manufacturer: "厂商", model: "型号", serial: "序列号", hardware: "硬件版本", firmware: "固件版本", software: "软件版本" };
    $(id).replaceChildren();
    Object.keys(fields).forEach(function (key) {
      var row = document.createElement("div");
      var label = document.createElement("dt");
      var value = document.createElement("dd");
      label.textContent = fields[key];
      value.textContent = (key === "name" ? name : info[key]) || missing;
      row.append(label, value);
      $(id).appendChild(row);
    });
  }

  function renderIdentity() {
    var device = devices[state.draft.device] || devices[state.draft.devices[0]]
      || devices[deviceKey("000000000000",0)];
    var canCopy = testedBridgeReady();
    if (!canCopy) state.copyIdentity = false;
    $("copyIdentity").checked = state.copyIdentity;
    $("copyIdentity").disabled = !canCopy;
    $("identityOptions").hidden = !state.copyIdentity;
    $("identityNameSource").value = state.nameSource;
    $("identityNameSource").options[0].disabled = !device.named;
    $("customNameField").hidden = state.nameSource !== "custom";
    $("customIdentityName").value = state.customName;
    fillInfo("meterInfo", device.info, !device.named ? "" : device.name, "未提供");
    var preview = pendingConfig();
    fillInfo("identityPreview", preview.identity, preview.deviceName, "未提供");
    fillInfo("boardIdentity", state.applied.identity, state.applied.deviceName, "未提供");
  }

  function renderDevice() {
    var device = devices[state.draft.device] || devices[state.draft.devices[0]]
      || devices[deviceKey("000000000000",0)];
    text("selectedDeviceName",device.name);
    text("selectedDeviceMeta","地址末尾 " + device.suffix + " · CPS");
    document.querySelector(".device-monogram").textContent = device.mark;
    $("deviceCapabilities").replaceChildren();
    ["功率", device.cadence ? "真实踏频" : "仅功率"].forEach(function (label) {
      var span = document.createElement("span");
      span.textContent = label;
      $("deviceCapabilities").appendChild(span);
    });
    renderIdentity();
  }

  function robotRange(simulation) {
    var presets = {}; state.pacers.forEach(function(p) { presets[String(p.id)] = [p.low / 10, p.high / 10]; });
    return presets[simulation.preset] || [simulation.wkgMin, simulation.wkgMax];
  }

  function validWkg(value) {
    return Number.isFinite(value) && value >= 1 && value <= 7
      && Math.abs(value * 10 - Math.round(value * 10)) < .000001;
  }
  function validRobot(simulation) {
    var range = robotRange(simulation);
    return Number.isInteger(simulation.weight) && simulation.weight >= 40 && simulation.weight <= 150
      && range.every(validWkg) && range[0] <= range[1];
  }
  function renderRobotPreview() {
    var simulation = state.draft.simulation;
    var range = robotRange(simulation), valid = validRobot(simulation), ftp = state.draft.heart.ftp;
    var ftpValid = Number.isInteger(ftp) && ftp >= 50 && ftp <= 500;
    var power = range.map(function(n) { return Math.floor(simulation.weight * Math.round(n * 10) / 10); });
    var percent = power.map(function(n) { return n / ftp * 100; });
    var preset = state.pacers.find(function(p) { return String(p.id) === (simulation.preset === "custom" ? "5":simulation.preset); });
    var label = simulation.preset === "custom" ? "自定义":preset ? preset.name:"正在读取档位…";
    if (range.every(validWkg) && range[0] <= range[1]) label += " · " + range[0].toFixed(1) + "–" + range[1].toFixed(1) + " W/kg";
    text("robotPresetLabel",label);
    $("robotPreset").setAttribute("aria-valuetext",label);
    text("robotPowerRange",valid ? power[0] + "–" + power[1] + " W":"—");
    text("robotFtpReference",ftpValid ? "参考 FTP：" + ftp + " W":"参考 FTP");
    text("robotFtpRange",valid && ftpValid ? Math.round(percent[0]) + "–" + Math.round(percent[1]) + "% FTP":"—");
    $("robotFtpTrack").toggleAttribute("hidden",!valid || !ftpValid);
    $("robotFtpScale").hidden = !valid || !ftpValid;
    $("robotFtpRelation").textContent = !valid ? "填写有效体重和强度后显示预览"
      : !ftpValid ? "FTP 需为 50–500 W 的整数，请在顶部修改"
      : (power[1] < ftp ? "低于 FTP":power[0] > ftp ? "高于 FTP":power[0] < ftp && power[1] > ftp ? "跨越 FTP":"达到 FTP")
        + " · 仅作相对参考" + (percent[1] > 150 ? "；图示上限为 150%，数值完整显示":"");
    if (valid && ftpValid) {
      var start = Math.min(percent[0],149), end = Math.min(percent[1],150);
      $("robotRangeBand").style.transform = "translateX(" + start + "px) scaleX(" + Math.max(1,end - start) / 150 + ")";
    }
    $("robotRangeError").hidden = valid;
    text("robotRangeError",range[0] > range[1] ? "强度下限不能大于上限" : "体重需为 40–150 kg 整数；强度需为 1–7 W/kg，步进 0.1");
  }

  var floatGroups = [
    ["simPower","powerFloat","powerInputHelp","目标功率",0,2000,100,"W"],
    ["simCadence","cadenceFloat","cadenceInputHelp","踏频",20,120,20,"rpm"],
    ["fixedHeart","hrFloat","hrFloatHelp","心率",60,200,20,"bpm"],
    [null,"runPaceFloat","runPaceFloatHelp","配速",0,0,60,"秒/km"],
    ["runCadence","runCadenceFloat","runCadenceFloatHelp","步频",120,220,20,"spm"]
  ];
  function renderFloatInputs() {
    floatGroups.forEach(function(g) {
      var errors = [], slider = $(g[1] + "Range");
      [g[0],g[1]].forEach(function(id,i) {
        if (!id) return;
        var node = $(id), value = node.value === "" ? NaN:Number(node.value);
        var min = i ? 0:g[4], max = i ? g[6]:g[5];
        var invalid = !Number.isInteger(value) || value < min || value > max;
        node.setAttribute("aria-invalid",String(invalid));
        node.disabled = state.busy || state.closed;
        if (invalid) errors.push((i ? g[3].replace("目标","") + "浮动":g[3]) + "需为 " + min + "–" + max + " " + g[7] + " 的整数");
        if (i) {
          if (!invalid) slider.value = value;
          slider.disabled = invalid || state.busy || state.closed;
          slider.setAttribute("aria-valuetext",invalid ? "请先修正浮动数值":"正负 " + value + " " + g[7]);
        }
      });
      var robot = state.draft.simulation.mode === "pacer";
      $(g[2]).textContent = errors.join("；") || (robot && g[0] === "simCadence"
        ? "踏频 20–120 rpm，填写整数；机器人浮动由引擎控制。"
        : (g[0] ? g[3] + " " + g[4] + "–" + g[5] + " " + g[7] + "；":"")
          + "浮动 0–" + g[6] + " " + g[7] + "。滑杆只调浮动。");
      $(g[2]).classList.toggle("field-error",errors.length > 0);
      $(g[2]).hidden = errors.length === 0;
      var pair = $(g[1]).closest(".paired-input");
      if (pair) pair.classList.toggle("is-invalid",errors.length > 0);
    });
    renderPaceSpeed();
  }

  function renderSimulation() {
    var simulation = state.draft.simulation;
    var robot = simulation.mode === "pacer";
    setActive("[data-sim-mode]", simulation.mode, "simMode");
    $("robotFields").hidden = !robot;
    $("fixedPowerField").hidden = robot;
    document.querySelectorAll("[data-fixed-float]").forEach(function(node) { node.hidden = robot; });
    $("customRobotFields").hidden = simulation.preset !== "custom";
    $("robotPreset").value = simulation.preset === "custom" ? "5":simulation.preset;
    setActive("[data-ride]",simulation.preset,"ride");
    fillValues("simulation");
    renderFloatInputs();
    $("customPacerName").parentElement.hidden = simulation.preset !== "custom";
    renderRobotPreview();
  }

  function renderCalibration() {
    var config = state.draft;
    var calibration = config.calibration;
    setActive("[data-calibration]", calibration.mode, "calibration");
    $("calibrationAdjustment").hidden = calibration.mode === "raw";
    text("actualCalibration",calibrationText(state.applied));
    var value = calibration.mode === "percent" ? calibration.percent : calibration.watts;
    var unit = calibration.mode === "percent" ? "%" : "W";
    var min = calibration.mode === "percent" ? -50 : -200;
    var max = 200;
    var step = calibration.mode === "percent" ? .1 : 1;
    text("calibrationLabel",calibration.mode === "percent" ? "调整比例" : "调整瓦数");
    text("calibrationUnit",unit);
    $("calibrationValue").min = min;
    $("calibrationValue").max = max;
    $("calibrationValue").step = step;
    $("calibrationValue").value = value;
    $("calibrationRange").min = min;
    $("calibrationRange").max = max;
    $("calibrationRange").step = step;
    $("calibrationRange").value = value;
    if (calibration.mode === "raw") {
      text("calibrationFormula","输出等于输入，不应用校正");
    } else if (calibration.mode === "percent") {
      text("calibrationFormula","输入 × (1 " + (value >= 0 ? "+" : "−") + " " + Math.abs(value) + " / 100)，0 W 保持为 0 W");
    } else {
      text("calibrationFormula","输入 " + (value >= 0 ? "+" : "−") + " " + Math.abs(value) + " W，0 W 保持为 0 W");
    }
    text("calibrationPreview",state.rawPower + " W → " + outputPower(config) + " W");
  }

  function renderHeart() {
    var heart = state.draft.heart;
    $("heartToggle").classList.toggle("is-on", heart.enabled);
    $("heartToggle").setAttribute("aria-checked", String(heart.enabled));
    $("heartToggle").querySelector("b").textContent = heart.enabled ? "已启用" : "已关闭";
    $("heartFields").hidden = !heart.enabled;
    document.querySelector('[data-broadcast="heart"]').checked = heart.enabled;
    setActive("[data-heart-mode]", heart.mode, "heartMode");
    $("linkedHeartFields").hidden = heart.mode !== "linked";
    $("fixedHeartFields").hidden = heart.mode !== "fixed";
    fillValues("heart");
    renderFloatInputs();
  }

  function renderRunning() {
    setActive("[data-run-mode]", state.draft.running.mode, "runMode");
    var run = state.draft.running;
    text("runModeSummary",run.mode === "fixed" ? "固定配速":"配速档");
    $("runRobotFields").hidden = run.mode === "fixed";
    $("fixedPaceFields").hidden = run.mode !== "fixed";
    $("runPaceFloatField").hidden = run.mode !== "fixed";
    $("runCustomFields").hidden = run.preset !== "custom";
    $("runCustomName").parentElement.hidden = run.preset !== "custom";
    $("runPreset").value = run.preset;
    fillValues("running");
    $("paceSeconds").value = Number.isInteger(run.paceSeconds) ? String(run.paceSeconds).padStart(2,"0"):run.paceSeconds;
    renderFloatInputs();
    renderPaceSpeed();
  }

  function renderPaceSpeed() {
    var r = state.draft.running, total = r.paceMinutes * 60 + r.paceSeconds;
    var valid = Number.isInteger(r.paceMinutes) && r.paceMinutes >= 2 && r.paceMinutes <= 20
      && Number.isInteger(r.paceSeconds) && r.paceSeconds >= 0 && r.paceSeconds <= 59
      && total >= 144 && total <= 1200;
    [["paceMinutes",2,20],["paceSeconds",0,59]].forEach(function(g) {
      var node = $(g[0]), value = node.value === "" ? NaN:Number(node.value);
      node.disabled = state.busy || state.closed;
      node.setAttribute("aria-invalid",String(!Number.isInteger(value) || value < g[1] || value > g[2] || total < 144 || total > 1200));
    });
    $("fixedPaceFields").classList.toggle("is-invalid",!valid || $("runPaceFloat").getAttribute("aria-invalid") === "true");
    text("paceSpeed",valid ? (3600 / total).toFixed(2) + " km/h":"请填写 2:24–20:00 的有效配速");
  }

  function renderAll() {
    renderHeader();
    renderSport();
    renderSource();
    renderDevice();
    renderSimulation();
    renderCalibration();
    renderHeart();
    renderRunning();
    renderLive();
    updateDirty();
  }

  function setCalibrationValue(value) {
    var calibration = state.draft.calibration;
    var min = calibration.mode === "percent" ? -50 : -200;
    var max = 200;
    var parsed = Number(value);
    if (!Number.isFinite(parsed)) return;
    parsed = Math.min(max, Math.max(min, parsed));
    if (calibration.mode === "percent") calibration.percent = Math.round(parsed * 10) / 10;
    else calibration.watts = Math.round(parsed);
    renderCalibration();
    updateDirty();
  }

  $("sharedHeart").appendChild($("heartSection"));
  document.querySelector(".diagnostics").appendChild(document.querySelector(".signal-chain"));
  function disclose(id, title, nodes) {
    var details = document.createElement("details"), summary = document.createElement("summary");
    var heading = document.createElement("div"), label = document.createElement("span");
    var value = document.createElement("b"), action = document.createElement("span");
    details.id = id; details.className = "device-settings";
    label.textContent = title; value.id = id + "Summary"; action.textContent = "展开";
    heading.append(label,value); summary.append(heading,action); details.appendChild(summary);
    nodes.forEach(function(node) { details.appendChild(node); });
    details.addEventListener("toggle",function() { action.textContent = details.open ? "收起":"展开"; });
    return details;
  }
  var sourceSection = document.querySelector("[data-source]").closest("section");
  var simulationModes = document.createElement("div");
  simulationModes.id = "simulationModes"; simulationModes.className = "editor-section";
  var modeLabel = document.createElement("p"); modeLabel.className = "identity-note"; modeLabel.textContent = "模拟模式";
  simulationModes.append(modeLabel,document.querySelector("[data-sim-mode]").parentElement);
  var sourceOptions = disclose("sourceOptions","模式与来源",[sourceSection,simulationModes]);
  $("sharedHeart").after(sourceOptions);
  sourceOptions.after(disclose("broadcastOptions","广播与兼容",[document.querySelector("[data-broadcast]").closest("section")]));
  var heartModes = document.querySelector("[data-heart-mode]").parentElement;
  var heartOptions = disclose("heartOptions","心率模式",[heartModes]);
  $("heartFields").prepend(heartOptions);
  function renderDisclosure() {
    var c = state.draft;
    $("sourceOptions").hidden = $("broadcastOptions").hidden = c.sport === "running";
    $("simulationModes").hidden = c.source === "bridge";
    $("sourceOptionsSummary").textContent = c.source === "bridge" ? "真实功率计"
      : "模拟输出 · " + (c.simulation.mode === "pacer" ? "配速机器人":"固定功率");
    text("heartOptionsSummary",c.heart.mode === "fixed" ? "固定心率":"功率随动");
    text("heartIndex",c.sport === "running" ? "02":c.source === "simulation" ? "01":"04");
    $("broadcastOptionsSummary").textContent = ["power","cadence","heart"].map(function(key,i) {
      return c.broadcast[key] ? ["功率","踏频","心率"][i]:"";
    }).filter(Boolean).join(" · ");
  }
  floatGroups.slice(2).forEach(function(g) {
    var input = $(g[1]), label = input.parentElement, field = document.createElement("div");
    field.id = g[1] + "Field"; field.className = "simulation-fields"; field.style.marginTop = "16px";
    label.before(field);
    var heading, pair, left;
    if (g[0]) {
      var target = $(g[0]), targetLabel = target.parentElement;
      targetLabel.parentElement.classList.remove("field-pair");
      heading = document.createElement("label"); heading.htmlFor = g[0];
      heading.textContent = targetLabel.firstChild.textContent;
      pair = document.createElement("div"); pair.className = "paired-input";
      left = document.createElement("div"); left.append(target,targetLabel.querySelector("span"));
      pair.appendChild(left); targetLabel.remove();
    } else {
      pair = $("fixedPaceFields"); pair.className = "paired-input pace-input";
      heading = pair.querySelector("label"); heading.htmlFor = "paceMinutes";
      left = pair.querySelector("div");
      left.querySelector("span").textContent = ":";
      left.querySelector("span:last-child").textContent = "/km";
      [["paceMinutes",2,20,"配速分钟"],["paceSeconds",0,59,"配速秒数"]].forEach(function(x) {
        $(x[0]).min = x[1]; $(x[0]).max = x[2]; $(x[0]).step = 1; $(x[0]).inputMode = "numeric";
        $(x[0]).setAttribute("aria-label",x[3]); $(x[0]).setAttribute("aria-describedby","paceSpeed " + g[2]);
      });
    }
    field.append(heading,pair);
    var right = document.createElement("div"), sign = document.createElement("span"), unit = document.createElement("span");
    sign.textContent = "±"; unit.textContent = g[7]; right.append(sign,input,unit); pair.appendChild(right);
    label.remove();
    input.min = 0; input.max = g[6]; input.step = 1; input.inputMode = "numeric";
    input.setAttribute("aria-label",g[3] + "浮动 " + g[7]);
    input.setAttribute("aria-describedby",g[2]); input.disabled = true;
    var slider = document.createElement("input");
    slider.id = g[1] + "Range"; slider.type = "range"; slider.min = 0; slider.max = g[6]; slider.step = 1;
    slider.disabled = true; slider.setAttribute("aria-label","调整" + g[3] + "浮动");
    slider.setAttribute("aria-describedby",g[2]);
    var help = document.createElement("p");
    help.id = g[2]; help.className = "identity-note"; help.setAttribute("role","status");
    field.append(slider,help);
    if (!g[0]) { $("paceSpeed").className = "identity-note"; field.appendChild($("paceSpeed")); }
    if (g[0]) {
      $(g[0]).min = g[4]; $(g[0]).max = g[5]; $(g[0]).step = 1;
      $(g[0]).inputMode = "numeric"; $(g[0]).setAttribute("aria-describedby",g[2]);
    }
  });
  function edited() { state.message = ""; updateDirty(); }
  ["sport","source","calibration","sim-mode","heart-mode","run-mode"].forEach(function(attr) {
    document.querySelectorAll("[data-" + attr + "]").forEach(function(button) {
      button.addEventListener("click",function() {
        if (!state.loaded || state.busy) return;
        var value = this.getAttribute("data-" + attr);
        if (attr === "sport") state.draft.sport = value;
        if (attr === "source") {
          state.draft.source = value;
          if (value === "simulation" && state.bridge && state.bridge.temporary) cancelTest();
        }
        if (attr === "calibration") state.draft.calibration.mode = value;
        if (attr === "sim-mode") state.draft.simulation.mode = value;
        if (attr === "heart-mode") state.draft.heart.mode = value;
        if (attr === "run-mode") state.draft.running.mode = value;
        renderAll(); edited();
      });
    });
  });
  function chooseRobot(value) {
    if (!state.loaded || state.busy || state.closed) return;
    state.draft.simulation.preset = value;
    renderAll(); edited();
  }
  $("robotPreset").addEventListener("input",function() { chooseRobot(this.value === "5" ? "custom":this.value); });
  [["runPreset","running"]].forEach(function(x) {
    $(x[0]).addEventListener("change",function() {
      state.draft[x[1]].preset = this.value;
      if (x[1] === "running" && this.value !== "custom") {
        var p = state.runPacers.find(function(p) { return String(p.id) === $("runPreset").value; });
        if (p) { state.draft.running.low = p.low; state.draft.running.high = p.high; state.draft.running.cadence = p.cadence; }
      }
      renderAll(); edited();
    });
  });
  document.querySelectorAll("[data-cadence-bundle]").forEach(function(input) {
    input.addEventListener("change", function() {
      state.draft.broadcast.cadenceWithPower = this.checked;
      if (this.checked && !state.draft.broadcast.cadence) {
        state.draft.broadcast.cadence = true;
        var cadenceCheck = document.querySelector('[data-broadcast="cadence"]');
        if (cadenceCheck) cadenceCheck.checked = true;
      }
      edited();
    });
  });
  document.querySelectorAll("[data-broadcast]").forEach(function(input) {
    input.addEventListener("change",function() {
      state.draft.broadcast[this.dataset.broadcast] = this.checked;
      if (this.dataset.broadcast === "heart") state.draft.heart.enabled = this.checked;
      if (!(state.draft.broadcast.power && state.draft.broadcast.cadence)) {
        state.draft.broadcast.cadenceWithPower = false;
        var bundleBox = document.querySelector("[data-cadence-bundle]");
        if (bundleBox) bundleBox.checked = false;
      }
      if (!(state.draft.broadcast.power || state.draft.broadcast.cadence || state.draft.broadcast.heart)) {
        this.checked = true; state.draft.broadcast[this.dataset.broadcast] = true;
        if (this.dataset.broadcast === "heart") state.draft.heart.enabled = true;
        showToast("至少保留一项广播数据");
      }
      renderHeart(); edited();
    });
  });
  $("heartToggle").addEventListener("click",function() {
    state.draft.heart.enabled = !state.draft.heart.enabled;
    state.draft.broadcast.heart = state.draft.heart.enabled;
    if (!(state.draft.broadcast.power || state.draft.broadcast.cadence || state.draft.broadcast.heart)) state.draft.broadcast.power = true;
    state.bridgeHeartInitialized = true; renderSource(); renderHeart(); edited();
  });
  $("copyIdentity").addEventListener("change",function() { state.copyIdentity = this.checked; renderIdentity(); edited(); });
  $("identityNameSource").addEventListener("change",function() { state.nameSource = this.value; renderIdentity(); edited(); });
  $("customIdentityName").addEventListener("input",function() { state.customName = this.value; fillInfo("identityPreview",pendingConfig().identity,pendingConfig().deviceName,"未提供"); edited(); });
  var fields = {simPower:["simulation","power"],simCadence:["simulation","cadence"],
    profileWeight:["simulation","weight"],wkgMin:["simulation","wkgMin"],wkgMax:["simulation","wkgMax"],
    customPacerName:["simulation","customName"],powerFloat:["simulation","powerFloat"],cadenceFloat:["simulation","cadenceFloat"],
    ftpValue:["heart","ftp"],thresholdHeart:["heart","threshold"],fixedHeart:["heart","fixed"],hrFloat:["heart","float"],
    paceMinutes:["running","paceMinutes"],paceSeconds:["running","paceSeconds"],runCadence:["running","cadence"],
    runCadenceFloat:["running","cadenceFloat"],runPaceFloat:["running","paceFloat"],runPaceLow:["running","low"],
    runPaceHigh:["running","high"],runCustomName:["running","customName"]};
  function fillValues(group) {
    Object.keys(fields).forEach(function(id) {
      var binding = fields[id];
      if (binding[0] === group) $(id).value = state.draft[group][binding[1]];
    });
  }
  var restoreButton = document.createElement("button");
  restoreButton.id = "restoreConfigBtn"; restoreButton.className = "text-btn"; restoreButton.type = "button";
  restoreButton.textContent = "恢复已应用配置"; restoreButton.disabled = true;
  $("draftChip").before(restoreButton);
  function validationErrors() {
    var c = pendingConfig(), errors = [], s = c.simulation, r = c.running;
    function check(id, valid, message) {
      $(id).setAttribute("aria-invalid",String(!valid || errors.some(function(e) { return e[0] === id; })));
      if (!valid) errors.push([id,message]);
    }
    var rules = [["profileWeight",40,150,"体重","kg"],["ftpValue",50,500,"FTP","W"],
      ["thresholdHeart",100,220,"阈值心率","bpm"],["paceMinutes",2,20,"配速分钟","分"],
      ["paceSeconds",0,59,"配速秒数","秒"],["runPaceLow",144,1200,"最快配速","秒/km"],
      ["runPaceHigh",144,1200,"最慢配速","秒/km"]];
    floatGroups.forEach(function(g) {
      if (g[0] && (g[0] !== "simPower" || (c.sport === "cycling" && c.source === "simulation" && s.mode === "fixed"))) {
        rules.push([g[0],g[4],g[5],g[3],g[7]]);
      }
      rules.push([g[1],0,g[6],g[3].replace("目标","") + "浮动",g[7]]);
    });
    rules.forEach(function(g) {
      var field = fields[g[0]], v = c[field[0]][field[1]];
      check(g[0],Number.isInteger(v) && v >= g[1] && v <= g[2],g[3] + "需为 " + g[1] + "–" + g[2] + " " + g[4] + " 的整数");
    });
    check("powerStepInput",Number.isInteger(c.powerStep) && c.powerStep >= 5 && c.powerStep <= 50,"按钮步进需为 5–50 W 的整数");
    var total = r.paceMinutes * 60 + r.paceSeconds;
    if (Number.isInteger(total)) check("paceMinutes",total >= 144 && total <= 1200,"配速需为 2:24–20:00");
    if (Number.isInteger(r.low) && Number.isInteger(r.high)) check("runPaceLow",r.low <= r.high,"最快配速不能大于最慢配速");
    if (c.sport === "cycling" && c.source === "simulation" && s.mode === "pacer" && s.preset === "custom") {
      ["wkgMin","wkgMax"].forEach(function(id) { check(id,validWkg(s[id]) && s.wkgMin <= s.wkgMax,"强度需为 1–7 W/kg，步进 0.1，下限不大于上限"); });
    }
    [["deviceNameInput",c.deviceName,23],["customPacerName",s.customName,31],["runCustomName",r.customName,31]].forEach(function(g) {
      check(g[0],textValid(g[1],g[2]) && (g[0] !== "deviceNameInput" || !!g[1].trim()),"名称需符合 " + g[2] + " UTF-8 字节限制，不含控制字符，设备名称不能为空");
    });
    if (state.copyIdentity && c.source === "bridge" && state.nameSource === "custom") check("customIdentityName",!!state.customName.trim(),"复制的设备名称不能为空");
    check("broadcastOptionsSummary",!c.broadcast.cadenceWithPower || (c.broadcast.power && c.broadcast.cadence),"踏频随功率需同时启用功率与踏频");
    return errors;
  }
  function resetDraft() {
    state.draft = clone(state.applied); state.copyIdentity = false;
    state.nameSource = "board"; state.customName = "";
    state.bridgeHeartInitialized = state.applied.devices.length > 0;
    state.replaceSlot = state.pendingReplacementKey = state.testCandidate = null;
  }
  $("locateErrorBtn").addEventListener("click",function() {
    if (state.busy || state.closed) return;
    var error = validationErrors()[0];
    if (!error) return;
    var node = $(error[0]);
    if (node.tagName === "B") node = node.closest("details").querySelector("summary");
    for (var parent = node; parent; parent = parent.parentElement) {
      if (parent.tagName === "DETAILS") parent.open = true;
      parent.hidden = false;
    }
    node.focus({preventScroll:true}); node.scrollIntoView({block:"center"});
  });
  $("restoreConfigBtn").addEventListener("click",async function() {
    if (!state.loaded || state.busy || state.closed || !isDirty()) return;
    setBusy(true);
    try {
      var cancel = state.testCandidate || (state.bridge && state.bridge.temporary) || !$("scanModal").hidden;
      state.testCandidate = null;
      if (cancel && !await cancelTest()) return;
      if (state.closed) return;
      resetDraft(); state.message = "已恢复设备已应用配置"; renderAll();
    } finally { setBusy(false); }
  });
  Object.keys(fields).forEach(function(id) {
    $(id).addEventListener("input",function() {
      if (!state.loaded || state.busy || state.closed) return;
      var x = fields[id];
      state.draft[x[0]][x[1]] = /Name$/.test(id) ? this.value : this.value === "" ? NaN:Number(this.value);
      if (["profileWeight","wkgMin","wkgMax","ftpValue","thresholdHeart"].includes(id)) renderRobotPreview();
      if (floatGroups.some(function(g) { return g[0] === id || g[1] === id; })) renderFloatInputs();
      if (id === "paceMinutes" || id === "paceSeconds") renderPaceSpeed();
      edited();
    });
  });
  floatGroups.forEach(function(g) {
    var id = g[1];
    $(id + "Range").addEventListener("input",function() {
      if (!state.loaded || state.busy || state.closed) return;
      $(id).value = this.value;
      $(id).dispatchEvent(new Event("input",{bubbles:true}));
    });
  });
  $("deviceNameInput").addEventListener("input",function() { state.draft.deviceName = this.value; edited(); });
  $("powerStepInput").addEventListener("input",function() { state.draft.powerStep = this.value === "" ? NaN:Number(this.value); edited(); });
  $("calibrationMinus").addEventListener("click",function() { setCalibrationValue(Number($("calibrationValue").value) - Number($("calibrationValue").step)); });
  $("calibrationPlus").addEventListener("click",function() { setCalibrationValue(Number($("calibrationValue").value) + Number($("calibrationValue").step)); });
  ["calibrationValue","calibrationRange"].forEach(function(id) {
    $(id).addEventListener("input",function() { setCalibrationValue(this.value); edited(); });
  });
  var modalFocus = null;
  function openModal(id) { modalFocus = document.activeElement; $(id).hidden = false; $(id).querySelector("button").focus(); }
  function closeModal(id) { $(id).hidden = true; if (modalFocus) modalFocus.focus(); }
  async function cancelTest() {
    try {
      await api("bridge/scan",{stop:true});
      if (state.bridge && state.bridge.temporary) {
        await api("bridge/control",{action:"stop"});
        if (state.applied.source === "bridge" && state.applied.sport === "cycling") await api("bridge/control",{action:"resume"});
      }
      state.testCandidate = state.pendingReplacementKey = null;
      $("replaceBridgeConfirm").hidden = true;
      return true;
    } catch (e) { showToast(e.message); state.message = e.message; return false; }
  }
  document.querySelectorAll("[data-close-modal]").forEach(function(button) {
    button.addEventListener("click",function() {
      closeModal(this.dataset.closeModal + "Modal");
      if (this.dataset.closeModal === "scan") cancelTest();
    });
  });
  document.addEventListener("keydown",function(e) {
    var modal = ["scanModal","closeModal"].map($).find(function(x) { return !x.hidden; });
    if (!modal) return;
    if (e.key === "Escape") { closeModal(modal.id); if (modal.id === "scanModal") cancelTest(); }
    if (e.key === "Tab") {
      var buttons = Array.from(modal.querySelectorAll("button,input,select")).filter(function(node) {
        return !node.disabled && node.getClientRects().length && node.tabIndex >= 0;
      });
      if (!buttons.length) return;
      var first = buttons[0], last = buttons[buttons.length - 1];
      if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); }
      if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
    }
  });
  async function scan() {
    if (state.testCandidate !== null) return;
    $("rescanBtn").disabled = true;
    try {
      await api("bridge/scan",{});
      renderScan(await api("bridge/scan"));
    }
    catch (e) { $("scanStatus").textContent = e.message; }
    finally { $("rescanBtn").disabled = false; }
  }
  ["scanBtn","bridgeStartScanBtn"].forEach(function(id) { $(id).addEventListener("click",function() { openModal("scanModal"); scan(); }); });
  $("rescanBtn").addEventListener("click",scan);
  function renderScan(data) {
    if (state.testCandidate !== null) return;
    $("scanStatus").textContent = data.scanning ? "正在扫描附近 CPS 功率计…":data.devices.length ? "选择功率计并踩动曲柄进行测试":"未发现 CPS 功率计，请唤醒后重新扫描";
    $("scanResults").replaceChildren();
    data.devices.forEach(function(item) {
      var row = document.createElement("article"), text = document.createElement("div");
      var name = document.createElement("b"), meta = document.createElement("span"), button = document.createElement("button");
      row.dataset.scanIndex = String(data.devices.indexOf(item));
      row.dataset.address = item.address;
      name.textContent = item.name || "未命名功率计"; meta.textContent = item.address + " · " + item.rssi + " dBm";
      var saved = state.draft.devices.includes(deviceKey(item.address,item.addr_type));
      text.append(name,meta); button.textContent = saved ? "已保存":"选择并测试"; button.type = "button";
      button.disabled = saved;
      button.addEventListener("click",async function() {
        button.disabled = true;
        try {
          await api("bridge/control",{action:"test",candidate:item.candidate});
          state.testCandidate = {address:item.address,type:item.addr_type};
          $("scanStatus").textContent = "正在连接；请踩动曲柄，等待两份有效通知（最长 30 秒）";
          $("scanResults").querySelectorAll("button").forEach(function(b) { b.disabled = true; });
        } catch (e) { button.disabled = false; $("scanStatus").textContent = e.message; }
      });
      row.append(text,button); $("scanResults").appendChild(row);
    });
  }
  async function save(close) {
    if (!state.loaded || state.busy || state.closed || state.closeRequested) return;
    if (close) {
      state.closeRequested = true;
      clearTimeout(state.activityTimer); state.activityTimer = 0; state.activityPending = false;
    }
    if (!close && (!validFields() || (state.draft.source === "bridge" && !bridgeReady()))) return;
    setBusy(true); state.message = "";
    var transaction = 0, confirmedFailure = false;
    try {
      var body = close ? {revision:state.revision}:Object.assign(
        encode(pendingConfig()),{revision:state.revision,copy_identity:state.copyIdentity});
      if (!close && state.replaceSlot !== null) body.replace_slot = state.replaceSlot;
      var accepted = await api(close ? "close-wifi":"config",body);
      transaction = accepted.transaction;
      for (var n = 0; n < 20; n++) {
        var runtime = await api("runtime");
        if (runtime.transaction === transaction && !runtime.pending) {
          if (runtime.result !== 0) {
            confirmedFailure = true;
            throw new Error("配置未保存（错误 " + runtime.result + "），草稿已保留");
          }
          if (close) { endSession(true,runtime.remaining_sec); }
          else { await load(false); if (!state.closed) showToast("配置已保存并应用"); }
          return;
        }
        await new Promise(function(resolve) { setTimeout(resolve,250); });
      }
      throw new Error("设备尚未确认保存，请稍后核实结果");
    } catch (e) {
      var message = !confirmedFailure && (transaction || e.resultUnknown)
        ? "保存结果待核实，请核对设备配置：" + e.message:e.message;
      showToast(message);
      try { await load(true); } catch (_) {}
      state.message = message; updateDirty();
    } finally {
      if (!state.closed) state.closeRequested = false;
      setBusy(false);
      if (!state.closed && state.activityPending && !state.activityTimer && !state.renewing) {
        state.activityTimer = setTimeout(function() { state.activityTimer = 0; renewSession(false); },ACTIVITY_MERGE_MS);
      }
      renderTimer();
    }
  }
  $("saveBtn").addEventListener("click",function() { save(false); });
  $("closeWifiBtn").addEventListener("click",function() { if (state.loaded) openModal("closeModal"); });
  $("confirmCloseBtn").addEventListener("click",function() { save(true); });
  $("stopBridgeBtn").addEventListener("click",async function() {
    var stopping = this.textContent !== "重新连接";
    if (stopping && this.dataset.armed !== "true") {
      this.dataset.armed = "true"; this.textContent = "再次点击确认停止"; return;
    }
    try { await api("bridge/control",{action:stopping ? "stop":"resume"}); this.textContent = stopping ? "重新连接":"停止桥接"; this.dataset.armed = "false"; }
    catch(e) { showToast(e.message); }
  });
  $("retryBridgeBtn").addEventListener("click",async function() {
    this.disabled = true;
    try {
      await api("bridge/control",{action:"retry"});
      showToast("已开始快速恢复");
      await poll();
    } catch(e) { showToast(e.message); }
    finally { this.disabled = false; }
  });
  $("confirmBridgeReplaceBtn").addEventListener("click",function() {
    if (!state.pendingReplacementKey) return;
    var slot = Number($("replaceBridgeSlot").value);
    if (!Number.isInteger(slot) || slot < 0 || slot >= state.draft.devices.length) return;
    state.draft.devices[slot] = state.pendingReplacementKey;
    state.draft.device = state.pendingReplacementKey;
    state.replaceSlot = slot;
    state.pendingReplacementKey = null;
    $("replaceBridgeConfirm").hidden = true;
    closeModal("scanModal");
    renderAll(); edited();
    showToast("替换已加入草稿，保存后生效");
  });
  $("renewBtn").addEventListener("click",function() { renewSession(true); });
  $("warningRenewBtn").addEventListener("click",function() { renewSession(true); });
  async function checkSession() {
    if (state.closeConfirmed) return;
    $("sessionRetryBtn").disabled = true;
    try {
      var data = await api("state");
      if (state.closeConfirmed) return;
      if (data.closing) { endSession(true,data.remaining_sec); return; }
      if (data.remaining_sec > 0) {
        state.closed = false; state.closeRequested = false;
        $("wifiClosed").hidden = true;
        document.querySelector(".page").inert = false;
        document.querySelector(".action-bar").inert = false;
        $("profileBar").inert = false;
        await load(true,data);
        if (state.closed) return;
        $("renewBtn").disabled = false;
        clearInterval(pollTimer); clearInterval(memoryTimer);
        pollTimer = setInterval(poll,1000); memoryTimer = setInterval(refreshMemory,10000);
        poll();
      }
    } catch(e) {
      if (state.closeConfirmed) return;
      text("wifiClosedText","设备暂时无法连接，关闭状态未确认。本页不会自动提交草稿；已发出的保存请求结果请重新连接后核对。");
    } finally { $("sessionRetryBtn").disabled = false; }
  }
  $("sessionRetryBtn").addEventListener("click",checkSession);
  function renderTimer() {
    if (!state.loaded || !state.wifiDeadline) return;
    var seconds = Math.max(0,Math.ceil((state.wifiDeadline - performance.now()) / 1000));
    if (state.closed) {
      if (state.closeConfirmed && seconds === 0) $("wifiClosedState").textContent = "WiFi 关闭流程已启动";
      return;
    }
    text("wifiTimer",String(Math.floor(seconds / 60)).padStart(2,"0") + ":" + String(seconds % 60).padStart(2,"0"));
    $("wifiTimer").classList.toggle("is-low",seconds <= CLOSE_WARNING_SEC);
    $("sessionWarning").hidden = seconds > CLOSE_WARNING_SEC || state.closeRequested;
    text("warningSeconds",String(seconds));
    $("warningDirty").hidden = !isDirty();
    if (seconds === 0 && !state.renewing && !state.closeRequested) {
      endSession(false,0); checkSession();
    }
  }
  async function refreshMemory() {
    if (state.closed) return;
    try {
      var memory = await api("mem_stats");
      $("heapValue").textContent = "当前 " + (memory.free / 1024).toFixed(1)
        + " KiB · 最低 " + (memory.min_free / 1024).toFixed(1) + " KiB";
    } catch (_) {
      text("heapValue","不可用");
    }
  }
  async function poll() {
    if (state.polling || state.closed || !state.loaded) return;
    state.polling = true;
    var version = state.leaseVersion, startedAt = performance.now();
    try {
      var o = await api("runtime");
      if (o.closing) { endSession(true,o.remaining_sec); return; }
      if (state.closed) return;
      state.output = o; state.bridge = o.bridge; state.bridgeRuntime = o.bridge_runtime;
      state.rawPower = o.bridge.raw_power;
      text("meterConnection",o.bridge.valid ? "数据有效":o.bridge.connected ? "等待数据":"未连接");
      state.runtime = o.bridge_runtime && o.bridge_runtime.actual_source === "simulation"
        ? "fallback":o.bridge.valid ? "normal":"stale";
      countdown(o.remaining_sec,version,startedAt);
      if (state.closed) return;
      var key = rememberBridge(o.bridge);
      if (state.testCandidate && key === deviceKey(state.testCandidate.address,state.testCandidate.type)) {
        if (o.bridge.tested && o.bridge.valid) {
          state.draft.device = key; state.testCandidate = null; state.bridgeStage = "tested";
          if (!state.bridgeHeartInitialized) {
            state.draft.heart.enabled = false; state.draft.broadcast.heart = false; state.bridgeHeartInitialized = true;
          }
          state.nameSource = devices[key].named ? "meter":"board";
          if (state.draft.devices.includes(key)) {
            closeModal("scanModal");
          } else if (state.draft.devices.length < 3) {
            state.draft.devices.push(key);
            state.replaceSlot = null;
            closeModal("scanModal");
          } else {
            state.pendingReplacementKey = key;
            var select = $("replaceBridgeSlot");
            select.replaceChildren();
            state.draft.devices.forEach(function(savedKey,index) {
              var option = document.createElement("option");
              option.value = String(index);
              option.textContent = (index + 1) + ". " + devices[savedKey].name;
              select.appendChild(option);
            });
            $("replaceBridgeConfirm").hidden = false;
            text("scanStatus","测试通过；列表已满，请选择要替换的功率计");
          }
          renderDevice(); renderSource(); renderHeart(); edited();
          if (!state.pendingReplacementKey) showToast("测试通过，保存后才会切换实际输出");
        } else if ([0,9].includes(o.bridge.state)) {
          state.testCandidate = null; $("scanStatus").textContent = "测试未通过，请唤醒功率计后重试";
        }
      }
      if (o.revision !== state.revision && !state.busy) {
        var dirty = isDirty(); await load(dirty);
        if (dirty) { state.message = "设备配置已变化；草稿保留，请核对后保存"; updateDirty(); }
      }
      renderLive();
      if (document.activeElement !== $("calibrationValue") && document.activeElement !== $("calibrationRange")) renderCalibration();
      updateDirty();
      if (!$("scanModal").hidden && state.testCandidate === null) renderScan(await api("bridge/scan"));
      if (o.closing) endSession(true,o.remaining_sec);
    } catch(e) {
      if (state.closed) return;
      text("liveSubtitle","设备暂时不可达，最后数据已停止更新");
      $("heroMetricValue").textContent = "—"; $("metricTwoValue").textContent = "—"; $("heartLiveValue").textContent = "—";
      state.message = e.message; updateDirty();
    } finally { state.polling = false; }
  }
  document.querySelector(".editor-pane").classList.add("is-busy");
  load(false).then(function() { setBusy(false); refreshMemory(); poll(); }).catch(function(e) {
    text("saveState",e.message + "；请刷新页面重试");
  });
  pollTimer = setInterval(poll,1000);
  setInterval(renderTimer,1000);
  memoryTimer = setInterval(refreshMemory,10000);
  document.addEventListener("visibilitychange",function() { if (!document.hidden) { renderTimer(); poll(); } });
}());
