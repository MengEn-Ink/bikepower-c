(function () {
  "use strict";

  const mock = window.PowerBridgeMock;
  const state = mock.createInitialState();

  function wait(ms) {
    return new Promise(function (resolve) {
      window.setTimeout(resolve, ms);
    });
  }

  function clone(value) {
    return mock.clone(value);
  }

  function getDevice(deviceId) {
    return mock.devices.find(function (device) {
      return device.id === deviceId;
    }) || null;
  }

  function clamp(value, min, max) {
    return Math.min(max, Math.max(min, value));
  }

  function applyCalibration(power, calibration) {
    if (power <= 0) {
      return 0;
    }

    if (calibration.mode === "watts") {
      return clamp(Math.round(power + calibration.watts), 0, 2000);
    }

    if (calibration.mode === "percent") {
      return clamp(Math.round(power * (1 + calibration.percent / 100)), 0, 2000);
    }

    return clamp(Math.round(power), 0, 2000);
  }

  function validateConfig(config) {
    if (!["simulation", "bridge"].includes(config.source)) {
      throw new Error("未知的功率来源");
    }

    if (config.source === "bridge" && !getDevice(config.deviceId)) {
      throw new Error("请先选择功率计");
    }

    if (config.calibration.watts < -200 || config.calibration.watts > 200) {
      throw new Error("瓦数校正必须在 -200 到 +200 W 之间");
    }

    if (config.calibration.percent < -50 || config.calibration.percent > 50) {
      throw new Error("百分比校正必须在 -50% 到 +50% 之间");
    }
  }

  async function getConfig() {
    await wait(120);
    return clone(state.config);
  }

  async function scanPowerMeters() {
    await wait(1300);
    return clone(mock.devices);
  }

  async function testDevice(deviceId, onProgress) {
    const device = getDevice(deviceId);
    if (!device) {
      throw new Error("设备已经离开扫描范围");
    }

    const stages = [
      ["正在连接 " + device.name + "…", 480],
      ["正在发现 Cycling Power Service…", 520],
      ["正在订阅功率通知…", 520],
      ["正在等待两份有效数据…", 620]
    ];

    for (const stage of stages) {
      if (onProgress) {
        onProgress(stage[0]);
      }
      await wait(stage[1]);
    }

    return {
      device: clone(device),
      rawPower: 236,
      cadence: device.cadence ? 91 : null
    };
  }

  async function saveConfig(config) {
    validateConfig(config);
    await wait(720);
    state.config = clone(config);
    state.config.version += 1;
    state.runtime.bridgeActive = state.config.source === "bridge";
    state.runtime.demoState = "normal";
    state.runtime.reconnectStartedAt = 0;
    return clone(state.config);
  }

  async function stopBridge() {
    await wait(420);
    state.runtime.bridgeActive = false;
    state.runtime.demoState = "stopped";
    return { ok: true };
  }

  async function setDemoState(nextState) {
    if (!["normal", "stale", "reconnecting"].includes(nextState)) {
      throw new Error("未知的演示状态");
    }

    await wait(100);
    state.runtime.demoState = nextState;
    if (nextState === "reconnecting") {
      state.runtime.reconnectStartedAt = Date.now();
    } else {
      state.runtime.reconnectStartedAt = 0;
    }
    if (nextState === "normal" && state.config.source === "bridge") {
      state.runtime.bridgeActive = true;
    }
    return { ok: true };
  }

  async function closeWifi() {
    await wait(360);
    state.runtime.wifiClosed = true;
    return { ok: true };
  }

  async function getStatus() {
    await wait(80);

    const config = state.config;
    const device = getDevice(config.deviceId);
    const now = Date.now();
    const phase = now / 2100;
    let rawPower = null;
    let outputPower = 0;
    let cadence = null;
    let age = null;
    let statusName = state.runtime.demoState;

    if (config.source === "simulation") {
      statusName = "normal";
      rawPower = clamp(Math.round(config.simulation.power + Math.sin(phase) * 4), 0, 2000);
      outputPower = rawPower;
      cadence = clamp(Math.round(config.simulation.cadence + Math.sin(phase * 0.8) * 2), 0, 250);
      age = 0.2;
    } else if (!state.runtime.bridgeActive || statusName === "stopped") {
      statusName = "stopped";
      outputPower = 0;
    } else if (statusName === "normal") {
      rawPower = clamp(Math.round(238 + Math.sin(phase) * 7), 0, 2000);
      outputPower = applyCalibration(rawPower, config.calibration);
      cadence = device && device.cadence ? Math.round(92 + Math.sin(phase * 0.7) * 3) : null;
      age = 0.4;
      state.runtime.packetCount += 1;
    } else if (statusName === "stale") {
      outputPower = 0;
      age = 4.8;
    } else if (statusName === "reconnecting") {
      outputPower = 0;
      age = null;
    }

    let heartRate = null;
    if (config.heart.enabled) {
      if (config.heart.mode === "fixed") {
        heartRate = Math.round(config.heart.fixed + Math.sin(phase * 0.4) * 2);
      } else {
        const loadPower = rawPower || 0;
        const ratio = clamp(loadPower / Math.max(config.heart.ftp, 1), 0, 1.6);
        heartRate = Math.round(82 + ratio * (config.heart.threshold - 82) * 0.72);
      }
    }

    const reconnectElapsed = state.runtime.reconnectStartedAt
      ? Math.floor((now - state.runtime.reconnectStartedAt) / 1000)
      : 0;

    return {
      source: config.source,
      state: statusName,
      bridgeActive: state.runtime.bridgeActive,
      device: clone(device),
      rawPower: rawPower,
      outputPower: outputPower,
      cadence: cadence,
      heartRate: heartRate,
      heartEnabled: config.heart.enabled,
      heartMode: config.heart.mode,
      sampleAge: age,
      packetCount: state.runtime.packetCount,
      discardedPackets: state.runtime.discardedPackets,
      reconnectRemaining: Math.max(0, 60 - reconnectElapsed),
      calibration: clone(config.calibration),
      version: config.version
    };
  }

  window.PowerBridgeApi = {
    getConfig: getConfig,
    getStatus: getStatus,
    scanPowerMeters: scanPowerMeters,
    testDevice: testDevice,
    saveConfig: saveConfig,
    stopBridge: stopBridge,
    setDemoState: setDemoState,
    closeWifi: closeWifi,
    getDevice: function (deviceId) {
      return clone(getDevice(deviceId));
    }
  };
})();
