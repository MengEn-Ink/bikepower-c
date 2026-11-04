(function () {
  "use strict";

  const api = window.PowerBridgeApi;
  const clone = window.PowerBridgeMock.clone;
  const view = {};
  const state = {
    actual: null,
    draft: null,
    status: null,
    wifiRemaining: 165,
    toastTimer: 0,
    stopConfirmTimer: 0
  };

  function $(id) {
    return document.getElementById(id);
  }

  function cacheElements() {
    [
      "wifiTimer", "extendWifiBtn", "runningTab", "liveStateLabel", "upstreamState",
      "appState", "powerOutputLabel", "outputPower", "rawPower", "appliedCorrection",
      "sampleAge", "cadenceSource", "cadenceValue", "cadenceUnit", "cadenceMeta",
      "heartSource", "heartValue", "heartUnit", "heartMeta", "signalStatus",
      "signalStatusTitle", "signalStatusCopy", "diagAddress", "diagRssi", "diagPackets",
      "diagCrank", "diagSubscribe", "diagReconnect", "draftBadge", "draftNotice",
      "draftNoticeText", "bridgeSettings", "simulationSettings", "deviceName",
      "deviceAddress", "deviceCapabilities", "deviceConnection", "noCadenceWarning",
      "replaceDeviceBtn", "stopBridgeBtn", "actualCalibration", "calibrationControl",
      "calibrationLabel", "calibrationInput", "calibrationUnit", "calibrationRange",
      "calibrationMinus", "calibrationPlus", "calibrationFormula", "calibrationPreview",
      "heartSectionIndex", "heartToggle", "heartConfigBody", "linkedHeartFields",
      "fixedHeartFields", "ftpInput", "thresholdHeartInput", "fixedHeartInput",
      "simPowerInput", "simCadenceInput", "saveButton", "saveCloseButton", "scanDialog",
      "closeScanButton", "scanProgress", "scanStatus", "deviceResults", "scanAgainButton",
      "closeWifiDialog", "cancelCloseWifiIcon", "cancelCloseWifiButton",
      "confirmCloseWifiButton", "wifiClosedOverlay", "returnPrototypeButton", "toast"
    ].forEach(function (id) {
      view[id] = $(id);
    });
  }

  function formatSigned(value, unit) {
    const numeric = Number(value);
    const sign = numeric > 0 ? "+" : "";
    return sign + numeric + " " + unit;
  }

  function calibrationText(calibration) {
    if (calibration.mode === "watts") {
      return formatSigned(calibration.watts, "W");
    }
    if (calibration.mode === "percent") {
      return formatSigned(calibration.percent, "%");
    }
    return "原值";
  }

  function calculatePreview(power, calibration) {
    if (power <= 0) {
      return 0;
    }
    if (calibration.mode === "watts") {
      return Math.min(2000, Math.max(0, Math.round(power + calibration.watts)));
    }
    if (calibration.mode === "percent") {
      return Math.min(2000, Math.max(0, Math.round(power * (1 + calibration.percent / 100))));
    }
    return Math.round(power);
  }

  function isDirty() {
    return JSON.stringify(state.actual) !== JSON.stringify(state.draft);
  }

  function showToast(message) {
    window.clearTimeout(state.toastTimer);
    view.toast.textContent = message;
    view.toast.classList.add("is-visible");
    state.toastTimer = window.setTimeout(function () {
      view.toast.classList.remove("is-visible");
    }, 2600);
  }

  function setButtonLabel(button, label) {
    const textNode = Array.from(button.childNodes).find(function (node) {
      return node.nodeType === Node.TEXT_NODE && node.nodeValue.trim();
    });
    if (textNode) {
      textNode.nodeValue = " " + label;
    }
  }

  function setActiveButtons(selector, value, dataKey) {
    document.querySelectorAll(selector).forEach(function (button) {
      button.classList.toggle("is-active", button.dataset[dataKey] === value);
    });
  }

  function renderDirtyState() {
    const dirty = isDirty();
    view.draftBadge.hidden = !dirty;
    view.draftNotice.hidden = !dirty;
    if (!dirty) {
      return;
    }

    if (state.draft.source !== state.actual.source) {
      view.draftNoticeText.textContent = "来源切换尚未应用，实际输出继续使用已保存配置。";
    } else if (state.draft.deviceId !== state.actual.deviceId) {
      view.draftNoticeText.textContent = "新功率计已通过测试，保存后才会替换当前桥接设备。";
    } else {
      view.draftNoticeText.textContent = "参数修改尚未应用，实时读数仍使用已保存配置。";
    }
  }

  function renderSourceSettings() {
    setActiveButtons("[data-source]", state.draft.source, "source");
    view.bridgeSettings.hidden = state.draft.source !== "bridge";
    view.simulationSettings.hidden = state.draft.source !== "simulation";
    view.heartSectionIndex.textContent = state.draft.source === "bridge" ? "04" : "02";
  }

  function renderDevice() {
    const device = api.getDevice(state.draft.deviceId);
    if (!device) {
      view.deviceName.textContent = "尚未选择功率计";
      view.deviceAddress.textContent = "扫描附近 CPS 设备";
      view.deviceCapabilities.replaceChildren();
      view.deviceConnection.textContent = "未选择";
      view.noCadenceWarning.hidden = true;
      return;
    }

    view.deviceName.textContent = device.name;
    view.deviceAddress.textContent = "地址末尾 " + device.addressSuffix + " · " + device.protocol;
    view.deviceCapabilities.replaceChildren();
    ["功率", device.cadence ? "真实踏频" : "仅功率"].forEach(function (label) {
      const tag = document.createElement("span");
      tag.textContent = label;
      view.deviceCapabilities.appendChild(tag);
    });
    view.deviceConnection.textContent = state.draft.deviceId === state.actual.deviceId ? "已连接" : "待应用";
    view.noCadenceWarning.hidden = device.cadence;
  }

  function renderCalibration(syncInput) {
    const calibration = state.draft.calibration;
    const mode = calibration.mode;
    setActiveButtons("[data-cal-mode]", mode, "calMode");
    view.calibrationControl.hidden = mode === "raw";
    view.actualCalibration.textContent = calibrationText(state.actual.calibration);

    let value = 0;
    let min = -200;
    let max = 200;
    let step = 1;
    let unit = "W";
    let label = "调整瓦数";

    if (mode === "watts") {
      value = calibration.watts;
    } else if (mode === "percent") {
      value = calibration.percent;
      min = -50;
      max = 50;
      step = 0.1;
      unit = "%";
      label = "调整比例";
    }

    if (syncInput && mode !== "raw") {
      view.calibrationInput.value = String(value);
      view.calibrationRange.value = String(value);
    }
    view.calibrationInput.min = String(min);
    view.calibrationInput.max = String(max);
    view.calibrationInput.step = String(step);
    view.calibrationRange.min = String(min);
    view.calibrationRange.max = String(max);
    view.calibrationRange.step = String(step);
    view.calibrationUnit.textContent = unit;
    view.calibrationLabel.textContent = label;

    const previewInput = state.status && state.status.rawPower !== null ? state.status.rawPower : 200;
    const previewOutput = calculatePreview(previewInput, calibration);
    if (mode === "watts") {
      view.calibrationFormula.textContent = "输入 " + formatSigned(calibration.watts, "W") + "；零功率保持为 0 W";
    } else if (mode === "percent") {
      const sign = calibration.percent >= 0 ? "+" : "−";
      view.calibrationFormula.textContent = "输入 × (1 " + sign + " " + Math.abs(calibration.percent) + " / 100)；零功率保持为 0 W";
    } else {
      view.calibrationFormula.textContent = "输出等于输入，不应用校正";
    }
    view.calibrationPreview.textContent = previewInput + " W → " + previewOutput + " W";
  }

  function renderHeartSettings() {
    const heart = state.draft.heart;
    view.heartToggle.classList.toggle("is-on", heart.enabled);
    view.heartToggle.setAttribute("aria-checked", String(heart.enabled));
    view.heartToggle.querySelector("b").textContent = heart.enabled ? "已启用" : "已关闭";
    view.heartConfigBody.hidden = !heart.enabled;
    setActiveButtons("[data-heart-mode]", heart.mode, "heartMode");
    view.linkedHeartFields.hidden = heart.mode !== "linked";
    view.fixedHeartFields.hidden = heart.mode !== "fixed";
    view.ftpInput.value = String(heart.ftp);
    view.thresholdHeartInput.value = String(heart.threshold);
    view.fixedHeartInput.value = String(heart.fixed);
  }

  function renderSimulationSettings() {
    setActiveButtons("[data-sim-mode]", state.draft.simulation.mode, "simMode");
    view.simPowerInput.value = String(state.draft.simulation.power);
    view.simCadenceInput.value = String(state.draft.simulation.cadence);
  }

  function renderDraft() {
    renderSourceSettings();
    renderDevice();
    renderCalibration(true);
    renderHeartSettings();
    renderSimulationSettings();
    renderDirtyState();
  }

  function renderStateLabel(status) {
    view.liveStateLabel.classList.remove("state-label--ok", "state-label--warn", "state-label--danger");
    if (status.state === "normal") {
      view.liveStateLabel.classList.add("state-label--ok");
      view.liveStateLabel.lastChild.nodeValue = " 数据有效";
    } else if (status.state === "stale") {
      view.liveStateLabel.classList.add("state-label--danger");
      view.liveStateLabel.lastChild.nodeValue = " 数据过期";
    } else if (status.state === "reconnecting") {
      view.liveStateLabel.classList.add("state-label--warn");
      view.liveStateLabel.lastChild.nodeValue = " 正在重连";
    } else {
      view.liveStateLabel.classList.add("state-label--danger");
      view.liveStateLabel.lastChild.nodeValue = " 桥接已停止";
    }
  }

  function renderSignalStatus(status) {
    view.signalStatus.classList.remove("is-danger", "is-warn");
    if (status.source === "simulation") {
      view.signalStatusTitle.textContent = "模拟输出正在运行";
      view.signalStatusCopy.textContent = "功率和踏频由本机平滑生成。";
      return;
    }

    if (status.state === "stale") {
      view.signalStatus.classList.add("is-danger");
      view.signalStatusTitle.textContent = "数据已过期，保护已生效";
      view.signalStatusCopy.textContent = "功率已归零，真实踏频已停止输出。";
    } else if (status.state === "reconnecting") {
      view.signalStatus.classList.add("is-warn");
      view.signalStatusTitle.textContent = "功率计断开，正在重连";
      view.signalStatusCopy.textContent = "剩余 " + status.reconnectRemaining + " 秒；不会切换到模拟数据。";
    } else if (status.state === "stopped") {
      view.signalStatus.classList.add("is-danger");
      view.signalStatusTitle.textContent = "桥接已停止";
      view.signalStatusCopy.textContent = "设备绑定和校正参数仍保留。";
    } else if (status.device && !status.device.cadence) {
      view.signalStatus.classList.add("is-warn");
      view.signalStatusTitle.textContent = "桥接正在运行，仅输出功率";
      view.signalStatusCopy.textContent = "所选功率计没有提供 CPS 曲柄数据。";
    } else {
      view.signalStatusTitle.textContent = "桥接正在运行";
      view.signalStatusCopy.textContent = "功率和真实踏频来自同一台功率计。";
    }
  }

  function renderLive(status) {
    state.status = status;
    const isSimulation = status.source === "simulation";
    const deviceName = status.device ? status.device.name : "未选择设备";

    renderStateLabel(status);
    view.upstreamState.textContent = isSimulation
      ? "本机模拟引擎"
      : (status.state === "normal" ? "已连接 · " + deviceName : deviceName + " · 未就绪");
    view.appState.textContent = "已连接 · 已订阅";
    view.powerOutputLabel.textContent = isSimulation ? "模拟功率" : "校正后功率";
    view.outputPower.textContent = String(status.outputPower);
    view.rawPower.textContent = status.rawPower === null ? "—" : String(status.rawPower);
    view.appliedCorrection.textContent = isSimulation
      ? "平滑浮动"
      : "已应用 " + calibrationText(status.calibration);
    view.sampleAge.textContent = status.sampleAge === null
      ? "当前没有有效通知"
      : "最后通知 " + status.sampleAge.toFixed(1) + " 秒前";

    if (status.cadence === null) {
      view.cadenceValue.textContent = "—";
      view.cadenceUnit.textContent = "";
      view.cadenceSource.textContent = isSimulation ? "模拟" : "真实 · 当前无效";
      view.cadenceMeta.textContent = status.device && !status.device.cadence
        ? "设备未提供曲柄数据"
        : "旧值不会继续输出";
    } else {
      view.cadenceValue.textContent = String(status.cadence);
      view.cadenceUnit.textContent = "rpm";
      view.cadenceSource.textContent = isSimulation ? "模拟 · 本机生成" : "真实 · 同一功率计";
      view.cadenceMeta.textContent = isSimulation ? "有界平滑变化" : "CPS 曲柄数据有效";
    }

    if (!status.heartEnabled || status.heartRate === null) {
      view.heartValue.textContent = "—";
      view.heartUnit.textContent = "";
      view.heartSource.textContent = "已关闭";
      view.heartMeta.textContent = "HRS 与 FTMS 心率字段停止";
    } else {
      view.heartValue.textContent = String(status.heartRate);
      view.heartUnit.textContent = "bpm";
      view.heartSource.textContent = status.heartMode === "linked" ? "模拟 · 功率联动" : "模拟 · 固定";
      view.heartMeta.textContent = status.heartMode === "linked"
        ? "基于原始功率平滑生成"
        : "按固定目标平滑生成";
    }

    renderSignalStatus(status);
    view.diagAddress.textContent = status.device ? "··:··:" + status.device.addressSuffix : "—";
    view.diagRssi.textContent = status.device ? status.device.rssi + " dBm · 扫描记录" : "—";
    view.diagPackets.textContent = status.packetCount.toLocaleString("zh-CN") + " · 丢弃 " + status.discardedPackets;
    view.diagCrank.textContent = status.cadence === null ? "无有效事件" : "有效 · Δ 668 ticks";
    view.diagSubscribe.textContent = status.cadence === null ? "CPS / FTMS" : "CPS / CSC / FTMS";
    view.diagReconnect.textContent = status.state === "reconnecting"
      ? "第 3 次 · 剩余 " + status.reconnectRemaining + "s"
      : "无";
    setActiveButtons("[data-demo-state]", status.state === "stopped" ? "stale" : status.state, "demoState");
    renderCalibration(false);
  }

  async function refreshStatus() {
    try {
      renderLive(await api.getStatus());
    } catch (error) {
      showToast("状态读取失败：" + error.message);
    }
  }

  function updateWifiTimer() {
    const minutes = Math.floor(state.wifiRemaining / 60);
    const seconds = state.wifiRemaining % 60;
    view.wifiTimer.textContent = String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0");
  }

  function startWifiTimer() {
    updateWifiTimer();
    window.setInterval(function () {
      if (state.wifiRemaining > 0 && view.wifiClosedOverlay.hidden) {
        state.wifiRemaining -= 1;
        updateWifiTimer();
      }
    }, 1000);
  }

  function updateCalibrationValue(value) {
    const mode = state.draft.calibration.mode;
    const numeric = Number(value);
    if (!Number.isFinite(numeric)) {
      return;
    }

    if (mode === "watts") {
      state.draft.calibration.watts = Math.min(200, Math.max(-200, Math.round(numeric)));
      value = state.draft.calibration.watts;
    } else if (mode === "percent") {
      state.draft.calibration.percent = Math.min(50, Math.max(-50, Math.round(numeric * 10) / 10));
      value = state.draft.calibration.percent;
    }
    view.calibrationInput.value = String(value);
    view.calibrationRange.value = String(value);
    renderCalibration(false);
    renderDirtyState();
  }

  async function startScan() {
    view.deviceResults.replaceChildren();
    view.scanAgainButton.hidden = true;
    view.scanProgress.hidden = false;
    view.scanProgress.classList.remove("is-complete");
    view.scanStatus.textContent = "正在扫描附近的功率计…";
    if (!view.scanDialog.open) {
      view.scanDialog.showModal();
    }

    try {
      const devices = await api.scanPowerMeters();
      view.scanProgress.classList.add("is-complete");
      view.scanStatus.textContent = "发现 " + devices.length + " 台 CPS 功率计";
      renderScanResults(devices);
      view.scanAgainButton.hidden = false;
    } catch (error) {
      view.scanStatus.textContent = "扫描失败：" + error.message;
      view.scanAgainButton.hidden = false;
    }
  }

  function renderScanResults(devices) {
    view.deviceResults.replaceChildren();
    devices.forEach(function (device) {
      const row = document.createElement("article");
      row.className = "result-row";

      const copy = document.createElement("div");
      const name = document.createElement("strong");
      const meta = document.createElement("span");
      const capability = document.createElement("em");
      name.textContent = device.name;
      meta.textContent = "地址末尾 " + device.addressSuffix + " · " + device.rssi + " dBm";
      capability.textContent = device.cadence ? "功率＋真实踏频" : "仅功率";
      copy.append(name, meta, capability);

      const select = document.createElement("button");
      select.type = "button";
      select.textContent = "选择并测试";
      select.addEventListener("click", async function () {
        document.querySelectorAll(".result-row button").forEach(function (button) {
          button.disabled = true;
        });
        view.scanProgress.hidden = false;
        view.scanProgress.classList.remove("is-complete");
        try {
          const result = await api.testDevice(device.id, function (message) {
            view.scanStatus.textContent = message;
          });
          state.draft.deviceId = result.device.id;
          renderDraft();
          view.scanProgress.classList.add("is-complete");
          view.scanStatus.textContent = result.cadence === null
            ? "测试成功：功率有效，设备未提供踏频"
            : "测试成功：" + result.rawPower + " W · " + result.cadence + " rpm";
          showToast("已选择 " + result.device.name + "，保存后应用");
          window.setTimeout(function () {
            if (view.scanDialog.open) {
              view.scanDialog.close();
            }
          }, 900);
        } catch (error) {
          view.scanStatus.textContent = "测试失败：" + error.message;
          document.querySelectorAll(".result-row button").forEach(function (button) {
            button.disabled = false;
          });
        }
      });

      row.append(copy, select);
      view.deviceResults.appendChild(row);
    });
  }

  async function performSave(closeWifi) {
    const buttons = [view.saveButton, view.saveCloseButton, view.confirmCloseWifiButton];
    buttons.forEach(function (button) {
      button.disabled = true;
    });
    setButtonLabel(view.saveButton, "正在保存…");

    try {
      const saved = await api.saveConfig(state.draft);
      state.actual = clone(saved);
      state.draft = clone(saved);
      renderDraft();
      await refreshStatus();
      showToast("配置已保存并应用");
      if (closeWifi) {
        await api.closeWifi();
        view.closeWifiDialog.close();
        view.wifiClosedOverlay.hidden = false;
      }
    } catch (error) {
      showToast("保存失败：" + error.message);
    } finally {
      buttons.forEach(function (button) {
        button.disabled = false;
      });
      setButtonLabel(view.saveButton, "保存并应用");
    }
  }

  function bindEvents() {
    document.querySelectorAll("[data-source]").forEach(function (button) {
      button.addEventListener("click", function () {
        state.draft.source = button.dataset.source;
        renderDraft();
      });
    });

    document.querySelectorAll("[data-cal-mode]").forEach(function (button) {
      button.addEventListener("click", function () {
        state.draft.calibration.mode = button.dataset.calMode;
        renderCalibration(true);
        renderDirtyState();
      });
    });

    document.querySelectorAll("[data-heart-mode]").forEach(function (button) {
      button.addEventListener("click", function () {
        state.draft.heart.mode = button.dataset.heartMode;
        renderHeartSettings();
        renderDirtyState();
      });
    });

    document.querySelectorAll("[data-sim-mode]").forEach(function (button) {
      button.addEventListener("click", function () {
        state.draft.simulation.mode = button.dataset.simMode;
        renderSimulationSettings();
        renderDirtyState();
      });
    });

    document.querySelectorAll("[data-demo-state]").forEach(function (button) {
      button.addEventListener("click", async function () {
        await api.setDemoState(button.dataset.demoState);
        await refreshStatus();
      });
    });

    view.calibrationInput.addEventListener("input", function () {
      updateCalibrationValue(view.calibrationInput.value);
    });
    view.calibrationRange.addEventListener("input", function () {
      updateCalibrationValue(view.calibrationRange.value);
    });
    view.calibrationMinus.addEventListener("click", function () {
      updateCalibrationValue(Number(view.calibrationInput.value) - Number(view.calibrationInput.step));
    });
    view.calibrationPlus.addEventListener("click", function () {
      updateCalibrationValue(Number(view.calibrationInput.value) + Number(view.calibrationInput.step));
    });

    view.heartToggle.addEventListener("click", function () {
      state.draft.heart.enabled = !state.draft.heart.enabled;
      renderHeartSettings();
      renderDirtyState();
    });

    view.ftpInput.addEventListener("input", function () {
      state.draft.heart.ftp = Number(view.ftpInput.value);
      renderDirtyState();
    });
    view.thresholdHeartInput.addEventListener("input", function () {
      state.draft.heart.threshold = Number(view.thresholdHeartInput.value);
      renderDirtyState();
    });
    view.fixedHeartInput.addEventListener("input", function () {
      state.draft.heart.fixed = Number(view.fixedHeartInput.value);
      renderDirtyState();
    });
    view.simPowerInput.addEventListener("input", function () {
      state.draft.simulation.power = Number(view.simPowerInput.value);
      renderDirtyState();
    });
    view.simCadenceInput.addEventListener("input", function () {
      state.draft.simulation.cadence = Number(view.simCadenceInput.value);
      renderDirtyState();
    });

    view.replaceDeviceBtn.addEventListener("click", startScan);
    view.scanAgainButton.addEventListener("click", startScan);
    view.closeScanButton.addEventListener("click", function () {
      view.scanDialog.close();
    });

    view.stopBridgeBtn.addEventListener("click", async function () {
      if (!view.stopBridgeBtn.classList.contains("is-confirming")) {
        view.stopBridgeBtn.classList.add("is-confirming");
        setButtonLabel(view.stopBridgeBtn, "再次点击确认停止");
        window.clearTimeout(state.stopConfirmTimer);
        state.stopConfirmTimer = window.setTimeout(function () {
          view.stopBridgeBtn.classList.remove("is-confirming");
          setButtonLabel(view.stopBridgeBtn, "停止桥接");
        }, 3500);
        return;
      }

      window.clearTimeout(state.stopConfirmTimer);
      await api.stopBridge();
      view.stopBridgeBtn.classList.remove("is-confirming");
      setButtonLabel(view.stopBridgeBtn, "停止桥接");
      showToast("桥接已停止，设备绑定仍保留");
      await refreshStatus();
    });

    view.saveButton.addEventListener("click", function () {
      performSave(false);
    });
    view.saveCloseButton.addEventListener("click", function () {
      view.closeWifiDialog.showModal();
    });
    view.cancelCloseWifiIcon.addEventListener("click", function () {
      view.closeWifiDialog.close();
    });
    view.cancelCloseWifiButton.addEventListener("click", function () {
      view.closeWifiDialog.close();
    });
    view.confirmCloseWifiButton.addEventListener("click", function () {
      performSave(true);
    });
    view.returnPrototypeButton.addEventListener("click", function () {
      view.wifiClosedOverlay.hidden = true;
      state.wifiRemaining = 180;
      updateWifiTimer();
      showToast("已返回原型演示");
    });

    view.extendWifiBtn.addEventListener("click", function () {
      state.wifiRemaining += 180;
      updateWifiTimer();
      showToast("配网时间已延长 3 分钟");
    });

    view.runningTab.addEventListener("click", function () {
      showToast("切换运动类型会停止当前桥接；本原型聚焦骑行配置");
    });
  }

  async function initialize() {
    cacheElements();
    state.actual = await api.getConfig();
    state.draft = clone(state.actual);
    bindEvents();
    renderDraft();
    await refreshStatus();
    startWifiTimer();
    window.setInterval(refreshStatus, 1000);
  }

  document.addEventListener("DOMContentLoaded", initialize);
})();
