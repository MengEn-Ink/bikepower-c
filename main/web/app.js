(function () {
  "use strict";
  var $ = function(id) { return document.getElementById(id); };
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
      bridge_addr_type:meter.addrType,bridge_name:meter.named ? meter.name:"",
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
    if (!state.draft) return false;
    var c = pendingConfig(), s = c.simulation, h = c.heart, r = c.running;
    var range = function(v,min,max) { return Number.isInteger(v) && v >= min && v <= max; };
    var bw = state.draft.broadcast;
    if (bw.cadenceWithPower && !(bw.power && bw.cadence)) return false;
    return validSimulation() && range(s.cadence,20,120) && range(s.powerFloat,0,100)
      && range(s.cadenceFloat,0,20) && range(h.ftp,50,500) && range(h.threshold,100,220)
      && range(h.fixed,60,200) && range(h.float,0,20) && range(c.powerStep,5,50)
      && range(r.paceMinutes,2,20) && range(r.paceSeconds,0,59)
      && range(r.paceMinutes * 60 + r.paceSeconds,144,1200)
      && range(r.cadence,120,220) && range(r.cadenceFloat,0,20) && range(r.paceFloat,0,60)
      && range(r.low,144,1200) && range(r.high,144,1200) && r.low <= r.high
      && textValid(c.deviceName,23) && !!c.deviceName.trim()
      && textValid(s.customName,31) && textValid(r.customName,31)
      && Number.isInteger(s.weight) && s.weight >= 40 && s.weight <= 150;
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
    $("closeWifiBtn").disabled = value;
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
    $("renewBtn").disabled = true;
    $("sessionWarning").hidden = true;
    $("wifiClosed").hidden = false;
    $("wifiClosed").dataset.confirmed = String(confirmed);
    $("wifiClosedTitle").textContent = confirmed ? "本次配网已结束":"配网时间已到";
    $("wifiClosedState").textContent = confirmed ? "正在关闭 WiFi":"设备状态待确认";
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
        $("sessionRenewError").textContent = "续期未确认，请检查热点连接；倒计时未延长。";
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
    if (event.target.closest && event.target.closest("#renewBtn,#warningRenewBtn,#confirmCloseBtn,#wifiClosed")) return;
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
    [["robotPreset",state.pacers,5],["runPreset",state.runPacers,4]].forEach(function(entry) {
      var select = $(entry[0]); select.replaceChildren();
      entry[1].filter(function(p) { return p.id !== entry[2]; }).forEach(function(p) {
        var option = document.createElement("option");
        option.value = String(p.id); option.textContent = p.name; select.appendChild(option);
      });
      var custom = document.createElement("option"); custom.value = "custom";
      custom.textContent = "自定义"; select.appendChild(custom);
    });
  }
  async function load(preserve, snapshot) {
    var version = state.leaseVersion, startedAt = performance.now();
    var data = snapshot || await api("state");
    if (state.closed) return data;
    state.wire = clone(data.config); state.revision = data.revision;
    state.pacers = data.pacers; state.runPacers = data.run_pacers; state.bridge = data.bridge;
    rememberBridge(data.bridge);
    state.applied = decode(data.config);
    state.applied.identity = data.identity || clone(emptyIdentity);
    if (!preserve || !state.draft) {
      state.draft = clone(state.applied); state.copyIdentity = false;
      state.bridgeHeartInitialized = state.applied.devices.length > 0;
      state.replaceSlot = null; state.pendingReplacementKey = null;
    }
    state.loaded = true; state.message = "";
    state.idleSeconds = data.idle_timeout_sec || state.idleSeconds;
    if (data.closing) { endSession(true,data.remaining_sec); return data; }
    countdown(data.remaining_sec,version,startedAt);
    fillPresets();
    [["firmwareVersion",data.firmware_version],["boardType",data.board],
      ["gitSha",data.git_sha],["buildTime",data.build_time]].forEach(function(x) { $(x[0]).textContent = x[1]; });
    $("schemaVersion").textContent = "schema v" + data.config.schema_ver;
    $("headerFirmware").textContent = "固件 v" + data.firmware_version;
    if (!preserve) renderAll(); else updateDirty();
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
    $("liveTitle").textContent = running ? "跑步广播":fallback ? "桥接模拟降级":bridge ? "真实功率桥接":"模拟骑行输出";
    $("liveSubtitle").textContent = "已应用配置 · App 连接 " + o.connections;
    $("heroMetricLabel").textContent = running ? "当前速度":bridge ? "校正后功率":"模拟功率";
    $("heroMetricValue").textContent = running ? (o.run_speed_x100 / 100).toFixed(2):o.power;
    $("heroMetricUnit").textContent = running ? "km/h":"W";
    $("heroMetricMeta").textContent = running ? "累计 " + o.run_distance_m + " m · " + o.run_elapsed_sec + " 秒":bridge && !fallback ? "原始输入 " + (active ? b.raw_power:"—") + " W":"目标 " + c.simulation.power + " W";
    $("metricTwoLabel").textContent = running ? "步频":bridge ? "真实踏频":"模拟踏频";
    $("metricTwoValue").textContent = o.cadence;
    $("metricTwoUnit").textContent = running ? "spm":"rpm";
    $("metricTwoMeta").textContent = bridge ? "同一功率计，未提供时为零":"本机平滑生成";
    $("heartLiveValue").textContent = c.heart.enabled ? o.heartrate:"—";
    $("heartLiveMeta").textContent = c.heart.enabled ? "模拟心率":"已关闭";
    document.querySelector(".signal-chain").hidden = !bridge;
    $("chainSource").textContent = fallback ? "模拟引擎":(devices[deviceKey(b.address,b.addr_type)] || devices[c.device]).name;
    $("chainSourceState").textContent = fallback ? "维持完整模拟输出":active ? labels[b.state]:"等待已保存功率计";
    $("chainTransform").textContent = calibrationText(c);
    $("liveStatus").className = "status-chip " + (valid ? "status-chip--ok":"status-chip--warn");
    $("liveStatus").textContent = running ? "跑步广播中":fallback ? "模拟降级":bridge ? (active ? labels[b.state]:"上游未连接"):"模拟运行";
    $("runtimeAlert").className = "runtime-alert" + (valid ? "":" is-warn");
    $("runtimeAlertTitle").textContent = running ? "跑步模拟正在运行":fallback ? "真实来源不可用，模拟输出继续":bridge ? (valid ? "桥接正在运行":"正在恢复真实功率计"):"模拟输出正在运行";
    $("runtimeAlertText").textContent = running ? "速度与步频由设备本机平滑生成。":fallback ? "设备会按低频节拍自动尝试已保存功率计。":bridge ? (valid ? "功率与踏频来自同一台功率计。":"快速恢复期间等待真实数据。"):"输出由设备本机平滑生成。";
    $("packetCount").textContent = b.valid ? (b.age_ms / 1000).toFixed(1) + " 秒":"—";
    $("eventStateLabel").textContent = running ? "步频状态":"曲柄事件";
    $("crankState").textContent = running ? (o.cadence > 0 ? "步频有效":"已停止"):(b.cadence > 0 ? "踏频有效":"未提供或已停止");
    $("appServices").textContent = running ? "RSC / HRS / FTMS":"CPS / CSC / FTMS";
    $("upstreamStateLabel").textContent = running ? "会话来源":"上游状态";
    $("reconnectState").textContent = running ? "本机模拟":labels[b.state] || "未知";
    renderBridgeRuntime();
  }

  function renderBridgeRuntime() {
    if (!state.draft || !state.applied) return;
    var runtime = state.bridgeRuntime || {
      state:"disabled", configured_real:state.applied.source === "bridge",
      actual_source:state.applied.source === "bridge" ? "real":"simulation",
      active_slot:-1, candidate_slot:-1, next_attempt_ms:0
    };
    $("configuredSource").textContent = state.applied.source === "bridge" ? "真实功率计":"模拟输出";
    var activeKey = runtime.active_slot >= 0 ? state.applied.devices[runtime.active_slot] : null;
    var activeDevice = activeKey ? devices[activeKey] : null;
    $("actualSource").textContent = runtime.state === "fast_recovery"
      ? "快速恢复中"
      : runtime.actual_source === "simulation" && runtime.configured_real
        ? "模拟降级"
        : activeDevice ? "真实功率计 · " + activeDevice.name:"模拟引擎";
    var seconds = Math.ceil((runtime.next_attempt_ms || 0) / 1000);
    $("nextBridgeAttempt").textContent = seconds > 0 ? seconds + " 秒":"—";
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
    $("toast").textContent = message;
    $("toast").classList.add("is-visible");
    state.toastTimer = setTimeout(function () { $("toast").classList.remove("is-visible"); }, 2200);
  }

  function updateDirty() {
    var dirty = isDirty();
    var bridgePending = state.draft.sport === "cycling"
      && state.draft.source === "bridge"
      && !bridgeReady();
    $("draftChip").hidden = !dirty;
    $("saveBtn").disabled = !state.loaded || state.busy || !dirty || bridgePending || !validFields();
    if (state.copyIdentity && state.draft.source === "bridge" && state.nameSource === "custom" && !state.customName.trim()) {
      $("saveBtn").disabled = true;
    }
    if (!validSimulation()) {
      $("saveBtn").disabled = true;
      $("saveHint").textContent = "请检查体重和强度范围";
    }
    $("saveState").textContent = state.busy ? "正在确认设备保存结果…" : state.message || (dirty ? "有未保存修改" : "当前配置已应用");
    $("saveHint").textContent = bridgePending
      ? "先选择并测试一台功率计"
      : (dirty ? "实际输出继续使用已应用配置" : "关闭网页不影响当前输出");
    $("saveBtn").textContent = bridgePending
      ? "选择功率计后继续"
      : (state.draft.source === "bridge" && state.draft.sport === "cycling" ? "保存并启用桥接" : "保存并应用");
    if (sportModePending()) {
      $("saveBtn").textContent = "保存并切换到" + (state.draft.sport === "running" ? "跑步":"骑行");
      $("saveHint").textContent = state.busy ? "正在切换运动模式…":"保存后 BLE 服务将短暂重建";
    }
    if (dirty && (state.draft.sport !== state.applied.sport
        || state.draft.source !== state.applied.source
        || JSON.stringify(state.draft.broadcast) !== JSON.stringify(state.applied.broadcast))) {
      $("saveHint").textContent = "保存后蓝牙将短暂断开，请在 App 或码表重新连接";
    }
    if (!validSimulation()) $("saveHint").textContent = "请检查体重和强度范围";
  }

  function renderHeader() {
    $("headerDeviceName").textContent = state.applied.deviceName;
    $("deviceNameInput").value = state.draft.deviceName;
    $("powerStepInput").value = state.draft.powerStep;
  }

  function renderSport() {
    var pending = sportModePending();
    setActive("[data-sport]", state.draft.sport, "sport");
    $("cyclingEditor").hidden = state.draft.sport !== "cycling";
    $("runningEditor").hidden = state.draft.sport !== "running";
    $("editorTitle").textContent = state.draft.sport === "cycling" ? "骑行输出" : "跑步输出";
    document.querySelector(".workspace").classList.toggle("is-mode-pending", pending);
    document.querySelector(".live-pane").hidden = pending;
    document.querySelector(".editor-pane").style.maxWidth = pending ? "760px":"";
    document.querySelector(".editor-pane").style.margin = pending ? "auto":"";
    $("modeSwitchNotice").hidden = !pending;
    $("modeSwitchTitle").textContent = state.draft.sport === "running" ? "跑步模式尚未应用":"骑行模式尚未应用";
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
    $("broadcastIndex").textContent = bridgeSelected ? (ready ? "04" : "03") : "03";
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
    $("selectedDeviceName").textContent = device.name;
    $("selectedDeviceMeta").textContent = "地址末尾 " + device.suffix + " · CPS";
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

  function validSimulation() {
    var simulation = state.draft.simulation;
    if (state.draft.sport !== "cycling" || state.draft.source !== "simulation") return true;
    if (simulation.mode === "fixed") return Number.isFinite(simulation.power) && simulation.power >= 0 && simulation.power <= 2000;
    var range = robotRange(simulation);
    return Number.isFinite(simulation.weight) && simulation.weight >= 40 && simulation.weight <= 150
      && range.every(function (n) { return Number.isFinite(n) && n >= 1 && n <= 7; })
      && range[0] <= range[1];
  }

  function renderRobotPreview() {
    var simulation = state.draft.simulation;
    var range = robotRange(simulation);
    $("robotPowerRange").textContent = validSimulation()
      ? Math.round(simulation.weight * range[0]) + "–" + Math.round(simulation.weight * range[1]) + " W" : "—";
    $("robotRangeError").hidden = validSimulation();
    $("robotRangeError").textContent = range[0] > range[1] ? "强度下限不能大于上限" : "体重需为 40–150 kg，强度需为 1–7 W/kg";
  }

  function renderSimulation() {
    var simulation = state.draft.simulation;
    var robot = simulation.mode === "pacer";
    setActive("[data-sim-mode]", simulation.mode, "simMode");
    $("simulationModeSummary").textContent = robot ? "按强度平滑输出" : "按填写功率输出";
    $("robotFields").hidden = !robot;
    $("fixedPowerField").hidden = robot;
    $("robotWeightField").hidden = !robot;
    $("customRobotFields").hidden = simulation.preset !== "custom";
    $("robotPreset").value = simulation.preset;
    $("simPower").value = simulation.power;
    $("simCadence").value = simulation.cadence;
    $("powerFloat").value = simulation.powerFloat;
    $("cadenceFloat").value = simulation.cadenceFloat;
    $("customPacerName").value = simulation.customName;
    $("customPacerName").parentElement.hidden = simulation.preset !== "custom";
    $("profileWeight").value = simulation.weight;
    $("wkgMin").value = simulation.wkgMin;
    $("wkgMax").value = simulation.wkgMax;
    renderRobotPreview();
  }

  function renderCalibration() {
    var config = state.draft;
    var calibration = config.calibration;
    setActive("[data-calibration]", calibration.mode, "calibration");
    $("calibrationAdjustment").hidden = calibration.mode === "raw";
    $("actualCalibration").textContent = calibrationText(state.applied);
    var value = calibration.mode === "percent" ? calibration.percent : calibration.watts;
    var unit = calibration.mode === "percent" ? "%" : "W";
    var min = calibration.mode === "percent" ? -50 : -200;
    var max = 200;
    var step = calibration.mode === "percent" ? .1 : 1;
    $("calibrationLabel").textContent = calibration.mode === "percent" ? "调整比例" : "调整瓦数";
    $("calibrationUnit").textContent = unit;
    $("calibrationValue").min = min;
    $("calibrationValue").max = max;
    $("calibrationValue").step = step;
    $("calibrationValue").value = value;
    $("calibrationRange").min = min;
    $("calibrationRange").max = max;
    $("calibrationRange").step = step;
    $("calibrationRange").value = value;
    if (calibration.mode === "raw") {
      $("calibrationFormula").textContent = "输出等于输入，不应用校正";
    } else if (calibration.mode === "percent") {
      $("calibrationFormula").textContent = "输入 × (1 " + (value >= 0 ? "+" : "−") + " " + Math.abs(value) + " / 100)，0 W 保持为 0 W";
    } else {
      $("calibrationFormula").textContent = "输入 " + (value >= 0 ? "+" : "−") + " " + Math.abs(value) + " W，0 W 保持为 0 W";
    }
    $("calibrationPreview").textContent = state.rawPower + " W → " + outputPower(config) + " W";
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
    $("ftpValue").value = heart.ftp;
    $("thresholdHeart").value = heart.threshold;
    $("fixedHeart").value = heart.fixed;
    $("hrFloat").value = heart.float;
  }

  function renderRunning() {
    setActive("[data-run-mode]", state.draft.running.mode, "runMode");
    var run = state.draft.running;
    $("runModeSummary").textContent = run.mode === "fixed" ? "固定配速":"配速档";
    $("runRobotFields").hidden = run.mode === "fixed";
    $("fixedPaceFields").hidden = run.mode !== "fixed";
    $("runCustomFields").hidden = run.preset !== "custom";
    $("runCustomName").parentElement.hidden = run.preset !== "custom";
    $("runPreset").value = run.preset;
    $("runPaceLow").value = run.low;
    $("runPaceHigh").value = run.high;
    $("runCustomName").value = run.customName;
    $("runPaceFloat").value = run.paceFloat;
    $("paceMinutes").value = state.draft.running.paceMinutes;
    $("paceSeconds").value = state.draft.running.paceSeconds;
    $("runCadence").value = state.draft.running.cadence;
    $("runCadenceFloat").value = state.draft.running.cadenceFloat;
    var total = Math.max(144, state.draft.running.paceMinutes * 60 + state.draft.running.paceSeconds);
    $("paceSpeed").textContent = (3600 / total).toFixed(2) + " km/h";
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
  function edited() { state.message = ""; updateDirty(); if (!validFields()) $("saveHint").textContent = "请检查输入范围、配速上下限和名称（最多 23 UTF-8 字节）"; }
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
  [["robotPreset","simulation"],["runPreset","running"]].forEach(function(x) {
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
  Object.keys(fields).forEach(function(id) {
    $(id).addEventListener("input",function() {
      if (!state.loaded) return;
      var x = fields[id];
      state.draft[x[0]][x[1]] = /Name$/.test(id) ? this.value : this.value === "" ? NaN:Number(this.value);
      if (["profileWeight","wkgMin","wkgMax"].includes(id)) renderRobotPreview();
      if (id === "paceMinutes" || id === "paceSeconds") {
        var r = state.draft.running;
        $("paceSpeed").textContent = (3600 / (r.paceMinutes * 60 + r.paceSeconds)).toFixed(2) + " km/h";
      }
      edited();
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
    state.testCandidate = null; state.pendingReplacementKey = null;
    $("replaceBridgeConfirm").hidden = true;
    try {
      await api("bridge/scan",{stop:true});
      if (state.bridge && state.bridge.temporary) {
        await api("bridge/control",{action:"stop"});
        if (state.applied.source === "bridge" && state.applied.sport === "cycling") await api("bridge/control",{action:"resume"});
      }
    } catch (e) { showToast(e.message); }
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
      var buttons = Array.from(modal.querySelectorAll("button:not(:disabled),input,select"));
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
            throw new Error(runtime.result === 3
              ? "设备忙碌，另一项配置或模式切换尚未结束，请稍后重试；草稿已保留"
              : "配置未保存（错误 " + runtime.result + "），草稿已保留");
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
      if (data.closing) { endSession(true,data.remaining_sec); return; }
      if (data.remaining_sec > 0) {
        state.closed = false; state.closeRequested = false;
        $("wifiClosed").hidden = true;
        document.querySelector(".page").inert = false;
        document.querySelector(".action-bar").inert = false;
        await load(true,data);
        if (state.closed) return;
        $("renewBtn").disabled = false;
        clearInterval(pollTimer); clearInterval(memoryTimer);
        pollTimer = setInterval(poll,1000); memoryTimer = setInterval(refreshMemory,10000);
        poll();
      }
    } catch(e) {
      $("wifiClosedText").textContent = "设备暂时无法连接，关闭状态未确认。本页不会自动提交草稿；已发出的保存请求结果请重新连接后核对。";
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
    $("wifiTimer").textContent = String(Math.floor(seconds / 60)).padStart(2,"0") + ":" + String(seconds % 60).padStart(2,"0");
    $("wifiTimer").classList.toggle("is-low",seconds <= CLOSE_WARNING_SEC);
    $("sessionWarning").hidden = seconds > CLOSE_WARNING_SEC || state.closeRequested;
    $("warningSeconds").textContent = String(seconds);
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
      $("heapValue").textContent = "不可用";
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
      $("meterConnection").textContent = o.bridge.valid ? "数据有效":o.bridge.connected ? "等待数据":"未连接";
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
            $("scanStatus").textContent = "测试通过；列表已满，请选择要替换的功率计";
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
      $("liveSubtitle").textContent = "设备暂时不可达，最后数据已停止更新";
      $("heroMetricValue").textContent = "—"; $("metricTwoValue").textContent = "—"; $("heartLiveValue").textContent = "—";
      state.message = e.message; updateDirty();
    } finally { state.polling = false; }
  }
  document.querySelector(".editor-pane").classList.add("is-busy");
  load(false).then(function() { setBusy(false); refreshMemory(); poll(); }).catch(function(e) {
    $("saveState").textContent = e.message + "；请刷新页面重试";
  });
  pollTimer = setInterval(poll,1000);
  setInterval(renderTimer,1000);
  memoryTimer = setInterval(refreshMemory,10000);
  document.addEventListener("visibilitychange",function() { if (!document.hidden) { renderTimer(); poll(); } });
}());
