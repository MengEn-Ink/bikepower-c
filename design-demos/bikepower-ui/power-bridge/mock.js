(function () {
  "use strict";

  const devices = [
    {
      id: "p505",
      name: "P505-4A29",
      addressSuffix: "A4:29",
      rssi: -48,
      protocol: "CPS",
      cadence: true
    },
    {
      id: "assioma",
      name: "ASSIOMA UNO",
      addressSuffix: "6C:18",
      rssi: -61,
      protocol: "CPS",
      cadence: true
    },
    {
      id: "power-only",
      name: "未命名功率计",
      addressSuffix: "B7:42",
      rssi: -69,
      protocol: "CPS",
      cadence: false
    }
  ];

  const initialConfig = {
    version: 13,
    source: "bridge",
    deviceId: "p505",
    calibration: {
      mode: "watts",
      watts: 12,
      percent: 5
    },
    heart: {
      enabled: true,
      mode: "linked",
      ftp: 250,
      threshold: 172,
      fixed: 140
    },
    simulation: {
      mode: "steady",
      power: 220,
      cadence: 90
    }
  };

  function clone(value) {
    return JSON.parse(JSON.stringify(value));
  }

  window.PowerBridgeMock = {
    devices: clone(devices),
    createInitialState: function () {
      return {
        config: clone(initialConfig),
        runtime: {
          bridgeActive: true,
          demoState: "normal",
          packetCount: 1284,
          discardedPackets: 0,
          reconnectStartedAt: 0,
          wifiClosed: false
        }
      };
    },
    clone: clone
  };
})();
