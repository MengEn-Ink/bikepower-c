(function () {
  "use strict";
  var $ = function(id) { return document.getElementById(id); };
  var clone = function(value) { return JSON.parse(JSON.stringify(value)); };
  var devices = {};
  var state = {loaded:false, busy:false, message:"正在读取设备…", applied:null, draft:null,
    wire:null, revision:0, runtime:"normal", rawPower:0, wifiDeadline:0,
    bridgeStage:"idle", bridgeHeartInitialized:false, copyIdentity:false,
    nameSource:"board", customName:"", toastTimer:0, pacers:[], runPacers:[],
    bridge:null, output:null, testCandidate:null, polling:false, closed:false};
  var emptyIdentity = {manufacturer:"",model:"",serial:"",hardware:"",firmware:"",software:""};
  function deviceKey(address, type) { return address + ":" + type; }
  function rememberBridge(b) {
    var key = deviceKey(b.address, b.addr_type);
    devices[key] = {name:b.device_name || b.name || "未命名功率计", named:!!(b.device_name || b.name),
      suffix:(b.address || "").match(/.{2}/g)?.reverse().join(":") || "—",
      cadence:b.cadence > 0, mark:"PM", info:b.identity || clone(emptyIdentity),
      address:b.address, addrType:b.addr_type};
    return key;
  }
  function decode(c) {
    var total = Math.round(360000 / c.run_speed_x100);
    var key = deviceKey(c.bridge_address, c.bridge_addr_type);
    if (!devices[key]) devices[key] = {name:c.bridge_name || "未命名功率计",named:!!c.bridge_name,
      suffix:c.bridge_address, cadence:false,mark:"PM",info:clone(emptyIdentity),
      address:c.bridge_address,addrType:c.bridge_addr_type};
    return {sport:c.sport_profile ? "running":"cycling", source:c.cycling_source ? "bridge":"simulation",device:key,
      calibration:{mode:["raw","watts","percent"][c.bridge_calibration],watts:c.bridge_watts,percent:c.bridge_percent_x10 / 10 - 100},
      simulation:{mode:c.mode === 1 ? "pacer":"fixed",power:c.power,cadence:c.cadence,weight:c.weight,
        preset:c.ride_type === 5 ? "custom":String(c.ride_type),wkgMin:c.custom_wkg_low / 10,
        wkgMax:c.custom_wkg_high / 10,customName:c.custom_pacer_name,powerFloat:c.power_float,cadenceFloat:c.cadence_float},
      broadcast:{power:!!(c.broadcast_mask & 1),cadence:!!(c.broadcast_mask & 2),heart:!!(c.broadcast_mask & 4)},
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
    var meter = devices[config.device];
    Object.assign(c, {sport_profile:config.sport === "running" ? 1:0,
      cycling_source:config.source === "bridge" ? 1:0,
      bridge_bound:meter.address !== "000000000000" ? 1:0,bridge_address:meter.address,
      bridge_addr_type:meter.addrType,bridge_name:meter.named ? meter.name:"",
      bridge_calibration:["raw","watts","percent"].indexOf(config.calibration.mode),
      bridge_watts:config.calibration.watts,bridge_percent_x10:Math.round((100 + config.calibration.percent) * 10),
      mode:s.mode === "pacer" ? 1:0,power:s.power,cadence:s.cadence,weight:s.weight,
      ride_type:s.preset === "custom" ? 5:Number(s.preset),custom_wkg_low:Math.round(s.wkgMin * 10),
      custom_wkg_high:Math.round(s.wkgMax * 10),custom_pacer_name:s.customName,
      power_float:s.powerFloat,cadence_float:s.cadenceFloat,has_user_hr:h.mode === "fixed" ? 1:0,
      heartrate:h.fixed,ftp:h.ftp,thr_hr:h.threshold,hr_float:h.float,
      broadcast_mask:(config.broadcast.power ? 1:0) | (config.broadcast.cadence ? 2:0) | (h.enabled ? 4:0),
      run_speed_x100:Math.round(360000 / (r.paceMinutes * 60 + r.paceSeconds)),
      run_mode:r.mode === "fixed" ? 0:1,run_pacer_type:r.preset === "custom" ? 4:Number(r.preset),
      run_pace_low_sec:r.low,run_pace_high_sec:r.high,run_custom_name:r.customName,
      run_pace_float_sec:r.paceFloat,run_cadence:r.cadence,run_cadence_float:r.cadenceFloat,
      device_name:config.deviceName,power_step:config.powerStep});
    return c;
  }
  function pendingConfig() {
    var c = clone(state.draft), d = devices[c.device];
    if (state.copyIdentity && c.source === "bridge") {
      Object.keys(d.info).forEach(function(k) { if (d.info[k]) c.identity[k] = d.info[k]; });
      if (state.nameSource === "meter" && d.named) c.deviceName = d.name;
      if (state.nameSource === "custom") c.deviceName = state.customName.trim();
    }
    return c;
  }
  function isDirty() { return state.loaded && (state.copyIdentity || JSON.stringify(encode(pendingConfig())) !== JSON.stringify(state.wire)); }
  function bridgeReady() {
    return state.bridge && state.bridge.tested
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
      var data = path === "renew" ? {remaining_sec:Number(await response.text())}:await response.json();
      if (data.ok === false) throw new Error(data.error || "设备拒绝操作");
      return data;
    } finally { clearTimeout(timeout); }
  }
  function setBusy(value) {
    state.busy = value;
    document.querySelector(".editor-pane").classList.toggle("is-busy",value);
    $("closeWifiBtn").disabled = value;
    if (state.loaded) updateDirty();
  }
  function countdown(seconds) { state.wifiDeadline = Date.now() + Math.max(0,seconds) * 1000; renderTimer(); }
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
  async function load(preserve) {
    var data = await api("state");
    state.wire = clone(data.config); state.revision = data.revision;
    state.pacers = data.pacers; state.runPacers = data.run_pacers; state.bridge = data.bridge;
    rememberBridge(data.bridge);
    state.applied = decode(data.config);
    state.applied.identity = data.identity || clone(emptyIdentity);
    if (!preserve || !state.draft) {
      state.draft = clone(state.applied); state.copyIdentity = false;
      state.bridgeHeartInitialized = !!data.config.bridge_bound;
    }
    state.loaded = true; state.message = ""; countdown(data.remaining_sec);
    fillPresets();
    [["firmwareVersion",data.firmware_version],["boardType",data.board],
      ["gitSha",data.git_sha],["buildTime",data.build_time]].forEach(function(x) { $(x[0]).textContent = x[1]; });
    $("headerFirmware").textContent = "固件 v" + data.firmware_version;
    if (!preserve) renderAll(); else updateDirty();
    return data;
  }
  function renderLive() {
    if (!state.output) return;
    var o = state.output, c = state.applied, b = o.bridge;
    var running = c.sport === "running", bridge = !running && c.source === "bridge";
    var active = bridge && !b.temporary && deviceKey(b.address,b.addr_type) === c.device;
    var valid = !bridge || (active && b.valid);
    var labels = ["未启动","连接中","读取服务","订阅中","等待功率","数据有效","数据过期","重连中","停止中","连接失败"];
    $("liveTitle").textContent = running ? "跑步广播":bridge ? "真实功率桥接":"模拟骑行输出";
    $("liveSubtitle").textContent = "已应用配置 · App 连接 " + o.connections;
    $("heroMetricLabel").textContent = running ? "当前速度":bridge ? "校正后功率":"模拟功率";
    $("heroMetricValue").textContent = running ? (o.run_speed_x100 / 100).toFixed(2):o.power;
    $("heroMetricUnit").textContent = running ? "km/h":"W";
    $("heroMetricMeta").textContent = running ? "累计 " + o.run_distance_m + " m · " + o.run_elapsed_sec + " 秒":bridge ? "原始输入 " + (active && b.valid ? b.raw_power:"—") + " W":"目标 " + c.simulation.power + " W";
    $("metricTwoLabel").textContent = running ? "步频":bridge ? "真实踏频":"模拟踏频";
    $("metricTwoValue").textContent = o.cadence;
    $("metricTwoUnit").textContent = running ? "spm":"rpm";
    $("metricTwoMeta").textContent = bridge ? "同一功率计，未提供时为零":"本机平滑生成";
    $("heartLiveValue").textContent = c.heart.enabled ? o.heartrate:"—";
    $("heartLiveMeta").textContent = c.heart.enabled ? "模拟心率":"已关闭";
    document.querySelector(".signal-chain").hidden = !bridge;
    $("chainSource").textContent = devices[c.device].name;
    $("chainSourceState").textContent = active ? labels[b.state]:"等待已绑定功率计";
    $("chainTransform").textContent = calibrationText(c);
    $("liveStatus").className = "status-chip " + (valid ? "status-chip--ok":"status-chip--warn");
    $("liveStatus").textContent = bridge ? (active ? labels[b.state]:"上游未连接"):"模拟运行";
    $("runtimeAlert").className = "runtime-alert" + (valid ? "":" is-warn");
    $("runtimeAlertTitle").textContent = bridge ? (valid ? "桥接正在运行":"真实数据不可用，功率归零"):"模拟输出正在运行";
    $("runtimeAlertText").textContent = bridge ? (valid ? "功率与踏频来自同一台功率计。":"请检查功率计；可重新连接或选择模拟后保存。"):"输出由设备本机平滑生成。";
    $("packetCount").textContent = b.valid ? (b.age_ms / 1000).toFixed(1) + " 秒":"—";
    $("crankState").textContent = b.cadence > 0 ? "踏频有效":"未提供或已停止";
    $("reconnectState").textContent = labels[b.state] || "未知";
  }
  function setActive(selector, value, key) {
    document.querySelectorAll(selector).forEach(function (button) {
      button.classList.toggle("is-active", button.dataset[key] === value);
    });
  }

  function outputPower(config) {
    if (config.source === "simulation") {
      if (config.simulation.mode === "fixed") return config.simulation.power;
      var range = robotRange(config.simulation);
      return Math.round(config.simulation.weight * (range[0] + range[1]) / 2);
    }
    if (state.runtime !== "normal") return 0;
    if (state.rawPower === 0) return 0;
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
    if (!validSimulation()) $("saveHint").textContent = "请检查体重和强度范围";
  }

  function renderHeader() {
    $("headerDeviceName").textContent = state.applied.deviceName;
    $("deviceNameInput").value = state.draft.deviceName;
    $("powerStepInput").value = state.draft.powerStep;
  }

  function renderSport() {
    setActive("[data-sport]", state.draft.sport, "sport");
    $("cyclingEditor").hidden = state.draft.sport !== "cycling";
    $("runningEditor").hidden = state.draft.sport !== "running";
    $("editorTitle").textContent = state.draft.sport === "cycling" ? "骑行输出" : "跑步输出";
  }

  function renderSource() {
    var bridgeSelected = state.draft.source === "bridge";
    var ready = bridgeSelected && bridgeReady();
    setActive("[data-source]", state.draft.source, "source");
    $("bridgeIntro").hidden = !bridgeSelected || ready;
    $("bridgeEditor").hidden = !ready;
    $("simulationEditor").hidden = state.draft.source !== "simulation";
    $("bridgeHeartHint").hidden = !ready;
    $("stopBridgeBtn").hidden = state.applied.source !== "bridge" || state.applied.sport !== "cycling";
    $("sourceSummary").textContent = bridgeSelected
      ? (state.applied.source === "bridge" && !isDirty()
        ? "真实功率 + 同源踏频"
        : (ready ? "功率计已测试，等待保存" : "尚未选择功率计"))
      : "本机平滑模拟";
    $("broadcastIndex").textContent = bridgeSelected ? (ready ? "04" : "03") : "03";
    $("heartIndex").textContent = bridgeSelected ? (ready ? "05" : "04") : "04";
    var powerCheck = document.querySelector('[data-broadcast="power"]');
    powerCheck.disabled = state.draft.source === "bridge";
    if (powerCheck.disabled) {
      powerCheck.checked = true;
      state.draft.broadcast.power = true;
    }
    document.querySelectorAll("[data-broadcast]").forEach(function (input) {
      input.checked = state.draft.broadcast[input.dataset.broadcast];
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
    var device = devices[state.draft.device];
    $("copyIdentity").checked = state.copyIdentity;
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
    var device = devices[state.draft.device];
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
    var max = calibration.mode === "percent" ? 50 : 200;
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
    var max = calibration.mode === "percent" ? 50 : 200;
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
  document.querySelectorAll("[data-broadcast]").forEach(function(input) {
    input.addEventListener("change",function() {
      state.draft.broadcast[this.dataset.broadcast] = this.checked;
      if (this.dataset.broadcast === "heart") state.draft.heart.enabled = this.checked;
      if (!Object.values(state.draft.broadcast).some(Boolean)) {
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
    if (!Object.values(state.draft.broadcast).some(Boolean)) state.draft.broadcast.power = true;
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
    state.testCandidate = null;
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
    try { renderScan(await api("bridge/scan",{})); }
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
      name.textContent = item.name || "未命名功率计"; meta.textContent = item.address + " · " + item.rssi + " dBm";
      text.append(name,meta); button.textContent = "选择并测试"; button.type = "button";
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
    if (!state.loaded || state.busy) return;
    if (!close && (!validFields() || (state.draft.source === "bridge" && !bridgeReady()))) return;
    setBusy(true); state.message = "";
    var transaction = 0;
    try {
      var body = close ? {revision:state.revision}:Object.assign(encode(pendingConfig()),{revision:state.revision,copy_identity:state.copyIdentity});
      var accepted = await api(close ? "close-wifi":"config",body);
      transaction = accepted.transaction;
      for (var n = 0; n < 20; n++) {
        var runtime = await api("runtime");
        if (runtime.transaction === transaction && !runtime.pending) {
          if (runtime.result !== 0) throw new Error("配置未保存（错误 " + runtime.result + "），草稿已保留");
          if (close) { state.closed = true; $("wifiClosed").hidden = false; closeModal("closeModal"); }
          else { await load(false); showToast("配置已保存并应用"); }
          return;
        }
        await new Promise(function(resolve) { setTimeout(resolve,250); });
      }
      throw new Error("设备尚未确认保存，请稍后核实结果");
    } catch (e) {
      state.message = transaction ? "保存结果待核实：" + e.message:e.message;
      showToast(state.message);
      try { await load(true); } catch (_) {}
    } finally { setBusy(false); }
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
  $("renewBtn").addEventListener("click",async function() {
    this.disabled = true;
    try { var data = await api("renew",{}); countdown(data.remaining_sec); showToast("配网时间已续期至 5 分钟"); }
    catch(e) { showToast(e.message); } finally { this.disabled = false; }
  });
  function renderTimer() {
    var seconds = Math.max(0,Math.ceil((state.wifiDeadline - Date.now()) / 1000));
    $("wifiTimer").textContent = String(Math.floor(seconds / 60)).padStart(2,"0") + ":" + String(seconds % 60).padStart(2,"0");
    $("wifiTimer").classList.toggle("is-low",seconds <= 60);
  }
  async function poll() {
    if (state.polling || state.closed || !state.loaded) return;
    state.polling = true;
    try {
      var o = await api("runtime");
      state.output = o; state.bridge = o.bridge; state.rawPower = o.bridge.raw_power;
      $("meterConnection").textContent = o.bridge.valid ? "数据有效":o.bridge.connected ? "等待数据":"未连接";
      state.runtime = o.bridge.valid ? "normal":"stale"; countdown(o.remaining_sec);
      var key = rememberBridge(o.bridge);
      if (state.testCandidate && key === deviceKey(state.testCandidate.address,state.testCandidate.type)) {
        if (o.bridge.tested && o.bridge.valid) {
          state.draft.device = key; state.testCandidate = null; state.bridgeStage = "tested";
          if (!state.bridgeHeartInitialized) {
            state.draft.heart.enabled = false; state.draft.broadcast.heart = false; state.bridgeHeartInitialized = true;
          }
          state.nameSource = devices[key].named ? "meter":"board";
          closeModal("scanModal"); renderDevice(); renderSource(); renderHeart(); edited();
          showToast("测试通过，保存后才会切换实际输出");
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
      if (o.closing) { state.closed = true; $("wifiClosed").hidden = false; }
    } catch(e) {
      $("liveSubtitle").textContent = "设备暂时不可达，最后数据已停止更新";
      $("heroMetricValue").textContent = "—"; $("metricTwoValue").textContent = "—"; $("heartLiveValue").textContent = "—";
      state.message = e.message; updateDirty();
    } finally { state.polling = false; }
  }
  document.querySelector(".editor-pane").classList.add("is-busy");
  load(false).then(function() { setBusy(false); poll(); }).catch(function(e) {
    $("saveState").textContent = e.message + "；请刷新页面重试";
  });
  setInterval(poll,1000); setInterval(renderTimer,1000);
  document.addEventListener("visibilitychange",function() { if (!document.hidden) poll(); });
}());
