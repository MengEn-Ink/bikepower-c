/* 此脚本只注入演示屏幕，所有接口由独立内存响应。 */
(function () {
  "use strict";
  const fixture = window.PHONE_FIXTURE;
  let config = structuredClone(fixture.state.config), revision = 1, transaction = 0;
  let result = 0, closing = false, failNext = false, peer = null, tested = false, temporary = false, raw = 200;
  let token = "", suspended = false;
  let runtimeReads = 0;
  const interval = window.setInterval.bind(window), clear = window.clearInterval.bind(window);
  const intervals = new Map();
  window.setInterval = (fn, ms) => {
    const key = intervals.size + 1;
    intervals.set(key, { fn, ms, handle: interval(fn, ms) });
    return key;
  };
  window.clearInterval = key => { const item = intervals.get(key); if (item) clear(item.handle); intervals.delete(key); };
  function pause(value) {
    if (suspended === value) return;
    suspended = value;
    intervals.forEach(item => {
      if (value) clear(item.handle);
      else item.handle = interval(item.fn, item.ms);
    });
  }
  function bridge() {
    const saved = config.bridge_devices[0];
    const active = peer || saved;
    const valid = !!active && (tested || !!saved);
    const power = !raw ? 0 : config.bridge_calibration === 1 ? raw + config.bridge_watts :
      config.bridge_calibration === 2 ? Math.round(raw * config.bridge_percent_x10 / 1000) : raw;
    return { state: valid ? 5 : 0, connected: valid, tested: valid, valid, temporary,
      raw_power: valid ? raw : 0, power: valid ? Math.max(0, Math.min(2000, power)) : 0,
      cadence: valid && raw ? 88 : 0, age_ms: 100, error: 0,
      device_name: active?.name || "", name: active?.name || "",
      address: active?.address || "000000000000", addr_type: active?.addr_type || 0,
      identity: active ? fixture.identity : {} };
  }
  function runtime() {
    const b = bridge(), real = !!config.cycling_source && b.valid && !temporary;
    return { revision, transaction, pending: false, result, restart: 0, source: config.cycling_source,
      power: real ? b.power : config.power, cadence: real ? b.cadence : config.cadence,
      heartrate: config.broadcast_mask & 4 ? config.heartrate : 0,
      connections: 0, remaining_sec: 300, closing, bridge: b,
      bridge_runtime: { state: real ? "live" : "disabled", configured_real: !!config.cycling_source,
        actual_source: real ? "real" : "simulation", active_slot: real ? 0 : -1,
        candidate_slot: -1, next_attempt_ms: 0 },
      run_speed_x100: config.run_speed_x100, run_distance_m: 0, run_elapsed_sec: 0 };
  }
  window.fetch = async (url, options = {}) => {
    if (typeof url !== "string" || !url.startsWith("/api/")) throw Error("演示禁止外部请求");
    const path = url.slice(5), body = options.body ? JSON.parse(options.body) : undefined;
    let data;
    if (path === "state") data = { ...fixture.state, config: structuredClone(config), revision, bridge: bridge(), closing, remaining_sec: 300 };
    else if (path === "runtime") { runtimeReads++; data = runtime(); }
    else if (path === "mem_stats") data = { free_heap: 140000, min_free_heap: 130000, largest_free_block: 100000 };
    else if (path === "renew") return new Response("300");
    else if (path === "bridge/scan") data = { scanning: !body?.stop, generation: 1, devices: fixture.devices };
    else if (path === "bridge/control") {
      if (body.action === "test") {
        peer = fixture.devices.find(item => item.candidate === body.candidate);
        tested = !!peer; temporary = true;
      } else if (body.action === "stop") { temporary = false; peer = null; tested = false; }
      data = { ok: true };
    } else if (path === "config" || path === "close-wifi") {
      if (body.revision !== revision) data = { ok: false, error: "配置已变化，请重试" };
      else {
        transaction++;
        result = failNext ? 5 : 0;
        failNext = false;
        if (!result) {
          if (path === "close-wifi") closing = true;
          else {
            const next = structuredClone(config);
            Object.keys(config).forEach(key => { if (key in body) next[key] = body[key]; });
            if (next.cycling_source && !next.bridge_devices.length) return Response.json({ ok: false, error: "尚未选择功率计" });
            config = next; revision++; temporary = false; peer = null;
          }
        }
        data = { ok: true, pending: true, transaction };
      }
    } else throw Error("演示未提供接口：" + path);
    return Response.json(data);
  };
  const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
  async function waitFor(check) {
    for (let i = 0; i < 80; i++) { if (check()) return; await delay(50); }
    throw Error("页面尚未出现预期结果，请重播");
  }
  function mark(node) {
    const rect = node.getBoundingClientRect();
    parent.postMessage({ type: "phone:target", token, rect: { x: rect.x, y: rect.y, width: rect.width, height: rect.height } }, "*");
  }
  let target = null;
  document.addEventListener("click", event => {
    const link = event.target.closest("a");
    if (link) { event.preventDefault(); window.scrollTo({ top: 0, behavior: "instant" }); }
  });
  window.addEventListener("pointerdown", event => { if (event.isTrusted) pause(false); }, true);
  window.addEventListener("keydown", event => { if (event.isTrusted) pause(false); }, true);
  window.addEventListener("scroll", () => { if (target) mark(target); }, true);
  window.addEventListener("message", async event => {
    if (event.source !== parent || event.data?.type !== "phone:command") return;
    const message = event.data;
    if (!token) token = message.token;
    if (!token || message.token !== token) return;
    try {
      const action = message.action;
      if (action.kind === "pause") { pause(action.value); return; }
      pause(false);
      if (action.kind === "ready") await waitFor(() => !document.querySelector(".editor-pane").classList.contains("is-busy"));
      else if (action.kind === "fault") failNext = true;
      else if (action.kind === "zero") {
        raw = 0;
        await waitFor(() => document.querySelector("#heroMetricValue").textContent === "0");
      }
      else {
        const node = document.querySelector(action.selector);
        if (!node) throw Error("页面控件已变化：" + action.selector);
        let ancestor = node.closest("details");
        if (ancestor && !ancestor.open) ancestor.open = true;
        if (!node.closest(".modal")) {
          const rect = node.getBoundingClientRect();
          window.scrollTo({ top: window.scrollY + rect.top + rect.height / 2 - innerHeight / 2, behavior: "instant" });
        }
        target = node; mark(node);
        if (action.kind === "input") {
          node.focus({ preventScroll: true }); node.value = action.value;
          node.dispatchEvent(new Event("input", { bubbles: true }));
          node.dispatchEvent(new Event("change", { bubbles: true }));
        } else if (action.kind === "click") {
          if (node.disabled) throw Error("控件尚不可用，请先完成前一步");
          node.click();
        }
        if (action.wait === "save") {
          await waitFor(() => document.querySelector("#toast").textContent === "配置已保存并应用" && !document.querySelector(".editor-pane").classList.contains("is-busy"));
          const before = runtimeReads;
          await waitFor(() => runtimeReads > before);
          await delay(0);
        }
        if (action.wait === "error") await waitFor(() => document.querySelector("#toast").textContent.includes("草稿已保留"));
        if (action.wait === "tested") await waitFor(() => document.querySelector("#scanModal").hidden && !document.querySelector("#saveBtn").disabled);
        if (action.wait === "closed") await waitFor(() => !document.querySelector("#wifiClosed").hidden);
      }
      parent.postMessage({ type: "phone:done", token, id: message.id, runtime: runtime() }, "*");
    } catch (error) {
      parent.postMessage({ type: "phone:done", token, id: message.id, error: error.message }, "*");
    }
  });
})();
