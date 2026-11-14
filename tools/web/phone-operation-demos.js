/* 手机外壳与操作时间轴；屏幕使用生产页面，消息限定来源与会话。 */
(function () {
  "use strict";
  const click = (selector, text, wait) => ({ kind: "click", selector, text, wait });
  const input = (selector, value, text) => ({ kind: "input", selector, value, text });
  const save = () => click("#saveBtn", "点击保存，等待页面确认已应用。", "save");
  const scenes = {
    save: { title: "修改与保存", note: "保存成功后再退出；失败时草稿保留，可以重试。",
      steps: [input("#simPower", "260", "点击真实目标输入框，改为 260 W。"),
        { kind: "fault", text: "演示一次保存失败，观察草稿保留。" },
        click("#saveBtn", "页面显示未保存，260 W 草稿仍在。", "error"), save(),
        click("#closeWifiBtn", "保存成功，再点击关闭 WiFi。"),
        click("#confirmCloseBtn", "确认关闭，回到你的运动 App。", "closed")] },
    bridge: { title: "功率计与校正", note: "扫描数据是演示样本。测试通过后还要保存；停踩保持 0 W。",
      steps: [click('[data-source="bridge"]', "选择真实功率计来源。"),
        click("#bridgeStartScanBtn", "点击选择功率计，打开真实扫描窗口。"),
        click('[data-scan-index="0"] button', "选择并测试，等待页面确认样本有效。", "tested"),
        click('[data-calibration="watts"]', "选择按瓦数校正。"),
        input("#calibrationValue", "20", "填写 +20 W，查看页面预览。"), save(),
        { kind: "zero", text: "模拟停踩：原始功率与校正输出都保持 0 W。" },
        { kind: "inspect", selector: "#heroMetricValue", text: "核对实际输出，停踩不会增加偏移。" }] },
    running: { title: "切换跑步", note: "保存运动类型后，重新在支持跑步传感器的 App 配对。",
      steps: [click('[data-sport="running"]', "点击跑步，页面显示尚未应用的提醒。"),
        input("#paceMinutes", "5", "设置每公里 5 分。"),
        input("#paceSeconds", "30", "设置每公里 5 分 30 秒。"), save(),
        { kind: "inspect", selector: "#heroMetricValue", text: "页面已切换跑步输出，再去 App 核对。" }] },
    cycling: { title: "设置骑行", note: "App 控制训练强度时，手动目标可能暂不生效。",
      steps: [input("#simPower", "260", "设置目标功率为 260 W。"), save(),
        click('[data-sim-mode="pacer"]', "也可以切换配速机器人，按体重与强度输出。"), save()] },
    profile: { title: "填写用户资料", note: "体重、FTP 和阈值心率固定在顶部，滚动时也能修改；保存后应用。",
      steps: [click('[data-sim-mode="pacer"]', "切换配速机器人，查看滑杆强度与功率预览。"),
        input("#profileWeight", "75", "在顶部填写体重 75 kg，预览随之更新。"), save()] },
    device: { title: "修改名称", note: "保存新名称后，可能需要删除旧配对再搜索。",
      steps: [input("#deviceNameInput", "我的训练伙伴", "展开设备设置，填写容易认出的名字。"), save(),
        { kind: "inspect", selector: "#headerDeviceName", text: "页面顶部已显示保存后的名称。" }] },
    step: { title: "调整按钮步进", note: "可设 5–50 W；保存后设备按新步进调节。",
      steps: [input("#powerStepInput", "10", "展开设备设置，改为每次 10 W。"), save(),
        { kind: "inspect", selector: "#powerStepInput", text: "保存回读后，步进仍为 10 W。" }] },
    about: { title: "核对设备版本", note: "演示显示样本版本。页面更新不会自动升级你的设备。",
      steps: [{ kind: "inspect", selector: "#firmwareVersion", text: "展开真实设备信息，查看固件版本。" },
        { kind: "inspect", selector: "#headerFirmware", text: "也可以从页面顶部核对版本。" }] }
  };
  scenes.upstream = scenes.bridge;
  scenes.exit = { ...scenes.save, title: "保存并退出", steps: [scenes.save.steps[0], save(), ...scenes.save.steps.slice(4)] };
  scenes.overview = scenes.exit;
  const portalGuides = new Set(Object.keys(scenes));
  let active = null, frame = null, token = "", sequence = 0, pending = new Map();
  let observer;
  function shell() {
    return `<div class="phone-stage"><div class="phone-fit"><div class="phone-body">
      <span class="phone-side-key" aria-hidden="true"></span>
      <div class="phone-status" aria-hidden="true"><b>9:41</b><svg viewBox="0 0 58 18"><path d="M2 15V11M7 15V8M12 15V5M17 15V2" stroke="currentColor" stroke-width="3"/><path d="M24 7q8-8 16 0m-13 3q5-5 10 0m-7 3 2 2 2-2" fill="none" stroke="currentColor" stroke-width="2"/><rect x="44" y="3" width="11" height="12" rx="2" fill="none" stroke="currentColor"/><path d="M56 7v4M46 6h7v6h-7" stroke="currentColor" stroke-width="2"/></svg></div>
      <div class="phone-camera" aria-hidden="true"></div><div class="phone-screen">
      <iframe title="真实配置页面 · 模拟演示" sandbox="allow-scripts" referrerpolicy="no-referrer"></iframe>
      <span class="phone-tap" aria-hidden="true"></span></div>
      <div class="phone-home" aria-hidden="true"><i></i></div>
      </div></div></div>`;
  }
  function markup(name) {
    const scene = scenes[name];
    return `<div class="phone-demo" data-phone-scene="${name}" data-playing="false">
      <div class="phone-demo-head"><h3>${scene.title}</h3><span>模拟演示 · 真实页面</span></div>
      <div data-phone-mount><div class="phone-placeholder">点击播放或下一步，打开手机演示</div></div>
      <p class="phone-caption" data-phone-caption role="status" aria-live="polite">可以跟随动画，也可以在屏幕里亲手操作。</p>
      <div class="demo-controls"><button class="btn primary" type="button" data-phone-play aria-pressed="false">播放演示</button>
      <button class="btn" type="button" data-phone-next>下一步</button><button class="btn ghost" type="button" data-phone-reset>重播</button>
      <span data-phone-count>0 / ${scene.steps.length}</span></div>
      <p class="operation-caution"><b>注意</b>${scene.note}</p></div>`;
  }
  function fit() {
    if (!frame) return;
    const root = frame.closest(".phone-stage"), holder = root.querySelector(".phone-fit");
    const scale = Math.min(1, (root.clientWidth - 32) / 418);
    holder.style.width = (418 * scale) + "px";
    holder.style.height = (872 * scale) + "px";
    holder.querySelector(".phone-body").style.transform = `scale(${scale})`;
  }
  function command(action) {
    return new Promise((resolve, reject) => {
      const id = ++sequence;
      const timer = setTimeout(() => { pending.delete(id); reject(Error("演示页面未响应，请重播")); }, 10000);
      pending.set(id, { resolve, reject, timer });
      frame.contentWindow.postMessage({ type: "phone:command", token, id, action }, "*");
    });
  }
  window.addEventListener("message", event => {
    if (!frame || event.source !== frame.contentWindow || event.data?.token !== token) return;
    const data = event.data;
    if (data.type === "phone:target") {
      const dot = frame.parentNode.querySelector(".phone-tap"), rect = data.rect;
      dot.style.left = Math.max(12, Math.min(378, rect.x + rect.width / 2)) + "px";
      dot.style.top = Math.max(12, Math.min(770, rect.y + rect.height / 2)) + "px";
      dot.classList.remove("is-tapping");
      void dot.offsetWidth;
      dot.classList.add("is-tapping");
    } else if (data.type === "phone:done" && pending.has(data.id)) {
      const item = pending.get(data.id);
      clearTimeout(item.timer); pending.delete(data.id);
      data.error ? item.reject(Error(data.error)) : item.resolve(data);
    }
  });
  async function activate(root, fresh = false) {
    if (active === root && frame && !fresh) return;
    if (active) active.pause();
    pending.forEach(item => { clearTimeout(item.timer); item.reject(Error("已切换演示")); });
    pending.clear();
    if (frame) frame.closest(".phone-stage").remove();
    active = root;
    root.querySelector("[data-phone-mount]").innerHTML = shell();
    frame = root.querySelector("iframe");
    const created = frame;
    token = crypto.randomUUID();
    fit();
    if (observer) observer.disconnect();
    observer = new ResizeObserver(fit);
    observer.observe(frame.closest(".phone-stage"));
    const loaded = new Promise(resolve => frame.addEventListener("load", resolve, { once: true }));
    frame.srcdoc = window.BikePowerPhoneSource;
    await loaded;
    if (frame !== created) throw Error("已切换演示");
    await command({ kind: "ready" });
    root.dataset.ready = "true";
  }
  function initRoot(root) {
    const scene = scenes[root.dataset.phoneScene], motion = matchMedia("(prefers-reduced-motion: reduce)");
    let position = 0, playing = false, busy = false, timer = 0, generation = 0;
    const lifecycle = new AbortController();
    const q = selector => root.querySelector(selector);
    root.pause = () => {
      playing = false; clearTimeout(timer);
      root.dataset.playing = "false";
      q("[data-phone-play]").textContent = "播放演示";
      q("[data-phone-play]").setAttribute("aria-pressed", "false");
      if (root === active && frame?.contentWindow) frame.contentWindow.postMessage({ type: "phone:command", token, action: { kind: "pause", value: true } }, "*");
    };
    async function next() {
      if (busy) return;
      busy = true;
      const current = generation;
      q("[data-phone-next]").disabled = true;
      try {
        if (position === scene.steps.length || active !== root) {
          position = 0; await activate(root, true);
        } else await activate(root);
        const action = scene.steps[position];
        q("[data-phone-caption]").textContent = action.text;
        await command(action);
        if (current !== generation) return;
        position++;
        q("[data-phone-count]").textContent = position + " / " + scene.steps.length;
        if (playing && position < scene.steps.length) timer = setTimeout(next, 1700);
        else if (playing) root.pause();
      } catch (error) {
        if (current === generation) { q("[data-phone-caption]").textContent = error.message; root.pause(); }
      } finally {
        busy = false; q("[data-phone-next]").disabled = false;
        if (!playing && active === root) root.pause();
      }
    }
    q("[data-phone-next]").onclick = () => { root.pause(); next(); };
    q("[data-phone-play]").onclick = () => {
      if (playing) { root.pause(); return; }
      if (motion.matches) { next(); return; }
      playing = true; root.dataset.playing = "true";
      q("[data-phone-play]").textContent = "暂停";
      q("[data-phone-play]").setAttribute("aria-pressed", "true");
      next();
    };
    q("[data-phone-reset]").onclick = async () => {
      root.pause(); generation++; position = 0;
      q("[data-phone-count]").textContent = "0 / " + scene.steps.length;
      q("[data-phone-caption]").textContent = "可以跟随动画，也可以在屏幕里亲手操作。";
      await activate(root, true); root.pause();
    };
    root.addEventListener("demo:pause", root.pause, { signal: lifecycle.signal });
    const details = root.closest("details");
    if (details) details.addEventListener("toggle", () => { if (!details.open) root.pause(); }, { signal: lifecycle.signal });
    const visibility = new IntersectionObserver(entries => { if (!entries[0].isIntersecting) root.pause(); });
    visibility.observe(root);
    document.addEventListener("visibilitychange", () => { if (document.hidden) root.pause(); }, { signal: lifecycle.signal });
    motion.addEventListener("change", root.pause, { signal: lifecycle.signal });
    root.dispose = () => { root.pause(); generation++; lifecycle.abort(); visibility.disconnect(); };
  }
  function mountGuide(host, name) {
    host.querySelector(".phone-demo")?.dispose();
    host.innerHTML = markup(name);
    initRoot(host.firstElementChild);
  }
  function mountSuite(host) {
    host.querySelectorAll('[data-operation-panel]:not([data-operation-panel="button"])').forEach(node => node.remove());
    host.querySelector(".operation-tabs").innerHTML = `<button type="button" data-operation-tab="button" aria-pressed="true">设备操作</button><button type="button" data-operation-tab="phone" aria-pressed="false">手机操作</button>`;
    const panel = document.createElement("section");
    panel.dataset.operationPanel = "phone"; panel.className = "hidden";
    panel.innerHTML = `<p class="phone-entry-note">实际使用前，先长按 + 二次确认开启设备热点，手机连接 BPW_XXXX（密码 12345678），再打开 http://192.168.4.1。下方是配置页模拟演示，不会连接或修改你的设备。</p><div class="phone-scenes" role="group" aria-label="选择手机操作">${["save", "bridge", "running", "cycling", "device"].map((name, i) =>
      `<button type="button" class="btn" data-phone-select="${name}" aria-pressed="${!i}">${scenes[name].title}</button>`).join("")}</div><div data-phone-suite></div>`;
    host.append(panel);
    mountGuide(panel.querySelector("[data-phone-suite]"), "save");
    panel.querySelectorAll("[data-phone-select]").forEach(button => button.onclick = () => {
      const old = panel.querySelector(".phone-demo");
      old.pause();
      panel.querySelectorAll("[data-phone-select]").forEach(item => item.setAttribute("aria-pressed", String(item === button)));
      mountGuide(panel.querySelector("[data-phone-suite]"), button.dataset.phoneSelect);
      const current = panel.querySelector(".phone-demo");
      activate(current).then(() => current.pause()).catch(() => {});
    });
    host.querySelector('[data-demo-phone]')?.addEventListener("click", () => {
      host.querySelector('[data-operation-tab="phone"]').click();
      panel.scrollIntoView({ block: "start", behavior: "auto" });
      panel.querySelector('[data-phone-select][aria-pressed="true"]').focus({ preventScroll: true });
    });
    host.querySelectorAll("[data-operation-tab]").forEach(button => button.onclick = () => {
      host.dispatchEvent(new Event("demo:pause"));
      panel.querySelector(".phone-demo").pause();
      host.querySelectorAll("[data-operation-tab]").forEach(item => item.setAttribute("aria-pressed", String(item === button)));
      host.querySelectorAll("[data-operation-panel]").forEach(item => item.classList.toggle("hidden", item.dataset.operationPanel !== button.dataset.operationTab));
      if (button.dataset.operationTab === "phone") {
        const current = panel.querySelector(".phone-demo");
        activate(current).then(() => current.pause()).catch(() => {});
      }
    });
  }
  window.BikePowerPhone = { mountSuite, mountGuide, portalGuides };
})();
