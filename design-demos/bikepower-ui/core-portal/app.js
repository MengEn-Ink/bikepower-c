(function () {
  "use strict";

  var $ = function (id) { return document.getElementById(id); };
  var clone = function (value) { return JSON.parse(JSON.stringify(value)); };
  var devices = {
    p505: { name: "P505-4A29", suffix: "A4:29", cadence: true, mark: "P5", info: { manufacturer: "Magene", model: "P505", serial: "DEMO-4A29", hardware: "1.0", firmware: "2.1", software: "" } },
    assioma: { name: "ASSIOMA DUO 721", suffix: "7C:21", cadence: true, mark: "AD", info: { manufacturer: "Favero", model: "ASSIOMA DUO", serial: "DEMO-00721", hardware: "1.0", firmware: "4.8", software: "" } },
    "4iiii": { name: "4iiii PRECISION", suffix: "1F:08", cadence: true, mark: "4I", info: { manufacturer: "4iiii", model: "PRECISION", serial: "DEMO-1F08", hardware: "2.0", firmware: "3.2", software: "" } },
    unnamed: { name: "未命名功率计", suffix: "B8:4D", cadence: false, mark: "PM", info: { manufacturer: "", model: "", serial: "", hardware: "", firmware: "", software: "" } }
  };
  var initialConfig = {
    sport: "cycling",
    source: "simulation",
    device: "p505",
    calibration: { mode: "watts", watts: 12, percent: 10 },
    simulation: { mode: "fixed", power: 220, cadence: 88, weight: 70, preset: "endurance", wkgMin: 2.5, wkgMax: 3 },
    broadcast: { power: true, cadence: true, heart: true },
    heart: { enabled: true, mode: "linked", ftp: 245, threshold: 178, fixed: 145 },
    running: { mode: "fixed", paceMinutes: 5, paceSeconds: 0, cadence: 170, cadenceFloat: 4 },
    deviceName: "BikePower-C3",
    identity: { manufacturer: "BikePower", model: "m5stamp-c3", serial: "C3-DEMO-001", hardware: "ESP32-C3", firmware: "4.2.1", software: "4.2.1" },
    powerStep: 20
  };
  var state = {
    applied: clone(initialConfig),
    draft: clone(initialConfig),
    runtime: "normal",
    rawPower: 238,
    wifiSeconds: 300,
    wifiDeadline: Date.now() + 300000,
    bridgeStage: "idle",
    bridgeHeartInitialized: false,
    copyIdentity: false,
    nameSource: "meter",
    customName: "",
    toastTimer: 0
  };

  function isDirty() {
    return JSON.stringify(state.applied) !== JSON.stringify(pendingConfig());
  }

  function pendingConfig() {
    var config = clone(state.draft);
    if (config.sport !== "cycling" || config.source !== "bridge" || !state.copyIdentity) return config;
    var device = devices[config.device];
    Object.keys(device.info).forEach(function (key) {
      if (device.info[key]) config.identity[key] = device.info[key];
    });
    if (state.nameSource === "meter" && config.device !== "unnamed") config.deviceName = device.name;
    if (state.nameSource === "custom") config.deviceName = state.customName.trim();
    return config;
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
      && state.bridgeStage !== "tested"
      && state.applied.source !== "bridge";
    $("draftChip").hidden = !dirty;
    $("saveBtn").disabled = !dirty || bridgePending;
    if (state.copyIdentity && state.draft.source === "bridge" && state.nameSource === "custom" && !state.customName.trim()) {
      $("saveBtn").disabled = true;
    }
    if (!validSimulation()) {
      $("saveBtn").disabled = true;
      $("saveHint").textContent = "请检查体重和强度范围";
    }
    $("saveState").textContent = dirty ? "有未保存修改" : "当前配置已应用";
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
    var bridgeReady = bridgeSelected
      && (state.bridgeStage === "tested" || state.applied.source === "bridge");
    setActive("[data-source]", state.draft.source, "source");
    $("bridgeIntro").hidden = !bridgeSelected || bridgeReady;
    $("bridgeEditor").hidden = !bridgeReady;
    $("simulationEditor").hidden = state.draft.source !== "simulation";
    $("bridgeHeartHint").hidden = !bridgeReady;
    $("stopBridgeBtn").hidden = state.applied.source !== "bridge" || state.applied.sport !== "cycling";
    $("sourceSummary").textContent = bridgeSelected
      ? (state.applied.source === "bridge" && !isDirty()
        ? "真实功率 + 同源踏频"
        : (bridgeReady ? "功率计已测试，等待保存" : "尚未选择功率计"))
      : "本机平滑模拟";
    $("broadcastIndex").textContent = bridgeSelected ? (bridgeReady ? "04" : "03") : "03";
    $("heartIndex").textContent = bridgeSelected ? (bridgeReady ? "05" : "04") : "04";
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
    $("identityNameSource").options[0].disabled = state.draft.device === "unnamed";
    $("customNameField").hidden = state.nameSource !== "custom";
    $("customIdentityName").value = state.customName;
    fillInfo("meterInfo", device.info, state.draft.device === "unnamed" ? "" : device.name, "未提供");
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
    var presets = { recovery: [1.5, 2], endurance: [2.5, 3], tempo: [3, 3.5] };
    return presets[simulation.preset] || [simulation.wkgMin, simulation.wkgMax];
  }

  function validSimulation() {
    var simulation = state.draft.simulation;
    if (state.draft.sport !== "cycling" || state.draft.source !== "simulation") return true;
    if (simulation.mode === "fixed") return Number.isFinite(simulation.power) && simulation.power >= 0 && simulation.power <= 2000;
    var range = robotRange(simulation);
    return Number.isFinite(simulation.weight) && simulation.weight >= 30 && simulation.weight <= 200
      && range.every(function (n) { return Number.isFinite(n) && n >= .5 && n <= 10; })
      && range[0] <= range[1];
  }

  function renderRobotPreview() {
    var simulation = state.draft.simulation;
    var range = robotRange(simulation);
    $("robotPowerRange").textContent = validSimulation()
      ? Math.round(simulation.weight * range[0]) + "–" + Math.round(simulation.weight * range[1]) + " W" : "—";
    $("robotRangeError").hidden = validSimulation();
    $("robotRangeError").textContent = range[0] > range[1] ? "强度下限不能大于上限" : "体重需为 30–200 kg，强度需为 0.5–10 W/kg";
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
  }

  function renderRunning() {
    setActive("[data-run-mode]", state.draft.running.mode, "runMode");
    $("paceMinutes").value = state.draft.running.paceMinutes;
    $("paceSeconds").value = state.draft.running.paceSeconds;
    $("runCadence").value = state.draft.running.cadence;
    $("runCadenceFloat").value = state.draft.running.cadenceFloat;
    var total = Math.max(144, state.draft.running.paceMinutes * 60 + state.draft.running.paceSeconds);
    $("paceSpeed").textContent = (3600 / total).toFixed(2) + " km/h";
  }

  function renderLive() {
    var applied = state.applied;
    var running = applied.sport === "running";
    var device = devices[applied.device];
    var bridge = !running && applied.source === "bridge";
    var normal = state.runtime === "normal";
    var power = outputPower(applied);
    $("liveTitle").textContent = running ? "跑步广播" : (bridge ? "真实功率桥接" : "模拟骑行输出");
    $("liveSubtitle").textContent = state.runtime === "normal" ? "已应用配置 · 最后通知 0.4 秒前" : (state.runtime === "stale" ? "保护已生效 · 数据超过 3 秒未更新" : "上游断开 · 限时重连中");
    $("heroMetricLabel").textContent = running ? "当前速度" : (bridge ? "校正后功率" : "模拟功率");
    $("heroMetricValue").textContent = running ? (3600 / Math.max(144, applied.running.paceMinutes * 60 + applied.running.paceSeconds)).toFixed(2) : power;
    $("heroMetricUnit").textContent = running ? "km/h" : "W";
    $("heroMetricMeta").textContent = running ? applied.running.paceMinutes + ":" + String(applied.running.paceSeconds).padStart(2, "0") + " /km" : (bridge ? "原始输入 " + (normal ? state.rawPower : "—") + " W" : (applied.simulation.mode === "pacer" ? "强度范围内输出" : "目标 " + applied.simulation.power + " W"));
    $("metricTwoLabel").textContent = running ? "步频" : (bridge ? "真实踏频" : "模拟踏频");
    $("metricTwoValue").textContent = normal ? (running ? applied.running.cadence : (bridge ? (device.cadence ? 92 : "—") : applied.simulation.cadence)) : "—";
    $("metricTwoUnit").textContent = running ? "spm" : "rpm";
    $("metricTwoMeta").textContent = running ? "本机平滑生成" : (bridge ? (device.cadence ? "同一功率计" : "设备未提供") : "本机平滑生成");
    $("heartLiveValue").textContent = applied.heart.enabled ? (applied.heart.mode === "fixed" ? applied.heart.fixed : (normal ? 148 : 112)) : "—";
    $("heartLiveMeta").textContent = applied.heart.enabled ? (applied.heart.mode === "fixed" ? "固定模拟" : "随原始功率联动") : "已关闭";
    $("chainSource").textContent = running ? "跑步引擎" : (bridge ? device.name : "模拟引擎");
    $("chainSourceState").textContent = normal ? "已连接" : (state.runtime === "stale" ? "数据过期" : "正在重连");
    $("chainTransform").textContent = running ? "RSC" : (bridge ? calibrationText(applied) : "平滑");
    document.querySelector(".signal-chain").hidden = !bridge;

    var status = $("liveStatus");
    status.className = "status-chip " + (normal ? "status-chip--ok" : (state.runtime === "stale" ? "status-chip--danger" : "status-chip--warn"));
    status.innerHTML = "<i></i>" + (normal ? "数据有效" : (state.runtime === "stale" ? "数据过期" : "正在重连"));
    var alert = $("runtimeAlert");
    alert.className = "runtime-alert" + (normal ? "" : (state.runtime === "stale" ? " is-danger" : " is-warn"));
    $("runtimeAlertTitle").textContent = normal ? (bridge ? "桥接正在运行" : "模拟输出正在运行") : (state.runtime === "stale" ? "数据已过期，保护已生效" : "功率计断开，正在重连");
    $("runtimeAlertText").textContent = normal ? (bridge ? "功率与踏频来自同一台功率计。" : "输出由设备本机平滑生成。") : (state.runtime === "stale" ? "功率已归零，真实踏频已停止输出。" : "剩余 42 秒，不会切换到模拟数据。");
    $("crankState").textContent = normal ? "有效" : "已失效";
    $("reconnectState").textContent = state.runtime === "reconnecting" ? "第 3 次" : "无";
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

  document.querySelectorAll("[data-sport]").forEach(function (button) {
    button.addEventListener("click", function () {
      state.draft.sport = button.dataset.sport;
      renderSport();
      updateDirty();
    });
  });
  document.querySelectorAll("[data-source]").forEach(function (button) {
    button.addEventListener("click", function () {
      state.draft.source = button.dataset.source;
      if (state.draft.source === "bridge" && state.applied.source !== "bridge" && state.bridgeStage === "idle") {
        state.bridgeStage = "select";
      }
      renderSource();
      renderCalibration();
      updateDirty();
    });
  });
  document.querySelectorAll("[data-calibration]").forEach(function (button) {
    button.addEventListener("click", function () {
      state.draft.calibration.mode = button.dataset.calibration;
      renderCalibration();
      updateDirty();
    });
  });
  document.querySelectorAll("[data-sim-mode]").forEach(function (button) {
    button.addEventListener("click", function () {
      state.draft.simulation.mode = button.dataset.simMode;
      renderSimulation();
      updateDirty();
    });
  });
  $("robotPreset").addEventListener("change", function () {
    state.draft.simulation.preset = this.value;
    renderSimulation();
    updateDirty();
  });
  document.querySelectorAll("[data-heart-mode]").forEach(function (button) {
    button.addEventListener("click", function () {
      state.draft.heart.mode = button.dataset.heartMode;
      renderHeart();
      updateDirty();
    });
  });
  document.querySelectorAll("[data-run-mode]").forEach(function (button) {
    button.addEventListener("click", function () {
      state.draft.running.mode = button.dataset.runMode;
      renderRunning();
      updateDirty();
    });
  });
  document.querySelectorAll("[data-runtime]").forEach(function (button) {
    button.addEventListener("click", function () {
      state.runtime = button.dataset.runtime;
      setActive("[data-runtime]", state.runtime, "runtime");
      renderLive();
    });
  });
  document.querySelectorAll("[data-broadcast]").forEach(function (input) {
    input.addEventListener("change", function () {
      state.draft.broadcast[input.dataset.broadcast] = input.checked;
      var active = Object.keys(state.draft.broadcast).filter(function (key) { return state.draft.broadcast[key]; });
      if (!active.length) {
        input.checked = true;
        state.draft.broadcast[input.dataset.broadcast] = true;
        showToast("至少保留一项广播数据");
      }
      if (input.dataset.broadcast === "heart") {
        state.draft.heart.enabled = input.checked;
        renderHeart();
      }
      updateDirty();
    });
  });
  document.querySelectorAll("[data-close-modal]").forEach(function (button) {
    button.addEventListener("click", function () {
      $(button.dataset.closeModal + "Modal").hidden = true;
    });
  });
  document.querySelectorAll("[data-select-device]").forEach(function (button) {
    button.addEventListener("click", function () {
      button.disabled = true;
      button.textContent = "测试中…";
      $("scanStatus").textContent = "正在连接并等待两份有效功率通知";
      setTimeout(function () {
        state.draft.device = button.dataset.selectDevice;
        if (!state.bridgeHeartInitialized) {
          state.draft.heart.enabled = false;
          state.draft.broadcast.heart = false;
          state.bridgeHeartInitialized = true;
        }
        if (state.draft.device === "unnamed" && state.nameSource === "meter") state.nameSource = "board";
        state.bridgeStage = "tested";
        $("scanModal").hidden = true;
        button.disabled = false;
        button.textContent = "选择并测试";
        $("scanStatus").textContent = "扫描完成 · 发现 3 台 CPS 功率计";
        renderDevice();
        renderSource();
        renderHeart();
        renderCalibration();
        updateDirty();
        showToast("测试通过，保存后应用新功率计");
      }, 700);
    });
  });

  $("calibrationMinus").addEventListener("click", function () { setCalibrationValue(Number($("calibrationValue").value) - Number($("calibrationValue").step)); });
  $("calibrationPlus").addEventListener("click", function () { setCalibrationValue(Number($("calibrationValue").value) + Number($("calibrationValue").step)); });
  $("calibrationValue").addEventListener("input", function () { setCalibrationValue(this.value); });
  $("calibrationRange").addEventListener("input", function () { setCalibrationValue(this.value); });
  $("heartToggle").addEventListener("click", function () {
    state.draft.heart.enabled = !state.draft.heart.enabled;
    state.draft.broadcast.heart = state.draft.heart.enabled;
    document.querySelector('[data-broadcast="heart"]').checked = state.draft.heart.enabled;
    renderHeart();
    updateDirty();
  });
  $("copyIdentity").addEventListener("change", function () {
    state.copyIdentity = this.checked;
    renderIdentity();
    updateDirty();
  });
  $("identityNameSource").addEventListener("change", function () {
    state.nameSource = this.value;
    renderIdentity();
    updateDirty();
  });
  $("customIdentityName").addEventListener("input", function () {
    state.customName = this.value;
    fillInfo("identityPreview", pendingConfig().identity, pendingConfig().deviceName, "未提供");
    updateDirty();
  });
  $("scanBtn").addEventListener("click", function () { $("scanModal").hidden = false; });
  $("bridgeStartScanBtn").addEventListener("click", function () { $("scanModal").hidden = false; });
  $("rescanBtn").addEventListener("click", function () {
    $("scanStatus").textContent = "正在扫描附近 CPS 功率计…";
    this.disabled = true;
    var button = this;
    setTimeout(function () {
      $("scanStatus").textContent = "扫描完成 · 发现 3 台 CPS 功率计";
      button.disabled = false;
    }, 800);
  });
  $("stopBridgeBtn").addEventListener("click", function () {
    if (this.dataset.armed !== "true") {
      this.dataset.armed = "true";
      this.textContent = "再次点击确认停止";
      showToast("停止后功率立即归零，绑定信息会保留");
      return;
    }
    state.runtime = "stale";
    setActive("[data-runtime]", state.runtime, "runtime");
    this.dataset.armed = "false";
    this.textContent = "停止桥接";
    renderLive();
  });
  $("saveBtn").addEventListener("click", function () {
    state.applied = pendingConfig();
    state.draft = clone(state.applied);
    state.copyIdentity = false;
    if (state.applied.source === "bridge") state.bridgeStage = "active";
    renderAll();
    showToast(state.applied.source === "bridge" && state.applied.sport === "cycling" ? "配置已保存，桥接已启用" : "配置已保存并应用");
  });
  $("closeWifiBtn").addEventListener("click", function () { $("closeModal").hidden = false; });
  $("confirmCloseBtn").addEventListener("click", function () {
    $("closeModal").hidden = true;
    $("wifiClosed").hidden = false;
  });
  $("restoreDemoBtn").addEventListener("click", function () { $("wifiClosed").hidden = true; });
  $("renewBtn").addEventListener("click", function () {
    state.wifiDeadline = Date.now() + 300000;
    renderTimer();
    showToast("配网时间已续期至 5 分钟");
  });

  [
    ["simPower", function (v) { state.draft.simulation.power = Number(v); }],
    ["simCadence", function (v) { state.draft.simulation.cadence = Number(v); }],
    ["profileWeight", function (v) { state.draft.simulation.weight = Number(v); renderRobotPreview(); }],
    ["wkgMin", function (v) { state.draft.simulation.wkgMin = Number(v); renderRobotPreview(); }],
    ["wkgMax", function (v) { state.draft.simulation.wkgMax = Number(v); renderRobotPreview(); }],
    ["ftpValue", function (v) { state.draft.heart.ftp = Number(v); }],
    ["thresholdHeart", function (v) { state.draft.heart.threshold = Number(v); }],
    ["fixedHeart", function (v) { state.draft.heart.fixed = Number(v); }],
    ["paceMinutes", function (v) { state.draft.running.paceMinutes = Number(v); renderRunning(); }],
    ["paceSeconds", function (v) { state.draft.running.paceSeconds = Number(v); renderRunning(); }],
    ["runCadence", function (v) { state.draft.running.cadence = Number(v); }],
    ["runCadenceFloat", function (v) { state.draft.running.cadenceFloat = Number(v); }],
    ["deviceNameInput", function (v) { state.draft.deviceName = v; renderHeader(); }],
    ["powerStepInput", function (v) { state.draft.powerStep = Number(v); }]
  ].forEach(function (binding) {
    $(binding[0]).addEventListener("input", function () {
      binding[1](this.value);
      updateDirty();
    });
  });

  function renderTimer() {
    state.wifiSeconds = Math.max(0, Math.ceil((state.wifiDeadline - Date.now()) / 1000));
    var minutes = Math.floor(state.wifiSeconds / 60);
    var seconds = state.wifiSeconds % 60;
    $("wifiTimer").textContent = String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0");
    $("wifiTimer").classList.toggle("is-low", state.wifiSeconds <= 60);
    $("renewBtn").title = state.wifiSeconds <= 60 ? "即将到期，续期 5 分钟" : "续期配网 5 分钟";
  }
  setInterval(renderTimer, 1000);
  document.addEventListener("visibilitychange", renderTimer);

  renderAll();
  renderTimer();
}());
