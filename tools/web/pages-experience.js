/* 页面教学独立于设备会话，不写入 BLE、串口或配置。 */
(function () {
  "use strict";
  const scenes = {
    short: { title: "短按减功率", duration: 1200, steps: [0, 300, 1000] },
    adjust: { title: "按住持续调功", duration: 2000, steps: [0, 500, 900, 1300, 1700] },
    wifi: { title: "长按 + 二次确认", duration: 4300, steps: [0, 500, 2100, 2300, 3300, 4000] },
    cancel: { title: "不确认，自动取消", duration: 6000, steps: [0, 2100, 2300, 3800, 5400] }
  };
  function buttonFrame(scene, elapsed) {
    const config = scenes[scene];
    if (!config) throw new Error("未知按钮演示");
    const t = Math.max(0, Math.min(Number(elapsed) || 0, config.duration));
    const release = scene === "short" ? 300 : scene === "adjust" ? 1500 : 2300;
    const pressed = t < release || scene === "wifi" && t >= 3300 && t < 3500;
    let power = 200, state = "ble", text = "按下设备操作键，开始计时。";
    if (scene === "short") {
      if (t >= release) { power = 180; text = "轻按松开，目标减少 20 W。"; }
    } else {
      const hold = Math.min(t, release, 1999);
      if (hold >= 500) power += (Math.floor((hold - 500) / 200) + 1) * 20;
      if (t >= 500) text = "继续按住，目标逐步增加。";
      if (scene === "adjust" && t >= release) text = "不到 2 秒松开，保留调节结果。";
      if (scene !== "adjust" && t >= 2000) text = "满 2 秒了，现在松开。";
      if (scene !== "adjust" && t >= release) {
        power = 200;
        state = "confirm";
        text = "灯快闪了，3 秒内再按一次。目标恢复原值。";
      }
      if (scene === "wifi" && t >= 3300) {
        state = "wifi";
        text = "热点已开启。去手机 WiFi 连接 BikePower-XXXXXX。";
      }
      if (scene === "cancel" && t >= 5300) {
        state = "ble";
        text = "没有再次确认，自动取消；目标保持原值。";
      }
    }
    const countdown = state === "confirm" ? Math.max(0, (5300 - t) / 1000) : null;
    const phase = t < release ? "hold" : state === "confirm" ? "release" : "confirm";
    const ledOn = state === "confirm" ? Math.floor(t / 200) % 2 === 0 : Math.floor(t / 1000) % 2 === 0;
    return { power, state, pressed, text, elapsed: t, progress: t / config.duration, countdown, phase, ledOn };
  }
  function releaseLabel(info) {
    if (!info) return "本地预览 · 未发布";
    if (!info.built_at || !/^[0-9a-f]{40}$/i.test(info.commit || "") || !Number.isFinite(Date.parse(info.built_at))) return "更新信息暂不可用";
    const date = new Intl.DateTimeFormat("zh-CN", {
      timeZone: "Asia/Shanghai", year: "numeric", month: "2-digit", day: "2-digit",
      hour: "2-digit", minute: "2-digit", hourCycle: "h23"
    }).format(new Date(info.built_at));
    return "页面更新发布时间 " + date + "（北京时间 · 页面构建） · " +
      info.commit.slice(0, 7) + (info.dirty ? " · 本地未提交预览" : "");
  }
  const api = { buttonFrame, releaseLabel, scenes };
  if (typeof module !== "undefined") module.exports = api;
  if (typeof window !== "undefined") window.BikePowerExperience = api;
  if (typeof document === "undefined") return;

  function initDemo(host) {
    let scene = "wifi", elapsed = 0, running = false, started = 0, request = 0;
    const play = host.querySelector("[data-demo-play]");
    const step = host.querySelector("[data-demo-step]");
    const caption = host.querySelector("[data-demo-caption]");
    const motion = matchMedia("(prefers-reduced-motion: reduce)");
    function paint(announce) {
      const frame = buttonFrame(scene, elapsed);
      host.dataset.state = frame.state;
      host.dataset.pressed = String(frame.pressed);
      host.dataset.playing = String(running);
      host.dataset.ledOn = String(frame.ledOn);
      host.querySelector("[data-demo-power]").textContent = frame.power;
      host.querySelector("[data-demo-time]").textContent = (frame.elapsed / 1000).toFixed(1) + " s";
      host.querySelector("[data-demo-fill]").style.transform = "scaleX(" + frame.progress + ")";
      host.querySelector("[data-demo-state]").textContent = {
        ble: "蓝牙工作", confirm: "等待二次确认", wifi: "WiFi 配网"
      }[frame.state];
      host.querySelector("[data-demo-text]").textContent = frame.text;
      const ledLabel = host.querySelector("[data-demo-led-label]");
      if (ledLabel) ledLabel.textContent = frame.state === "confirm" ? "指示灯 · 快闪确认" : frame.state === "wifi" ? "指示灯 · 配网节奏" : "指示灯 · 慢闪";
      host.querySelector("[data-demo-key]").textContent = frame.pressed ? "按住" : "松开";
      const countdown = host.querySelector("[data-demo-countdown]");
      if (countdown) countdown.textContent = frame.countdown !== null ? "确认剩余 " + frame.countdown.toFixed(1) + " s" :
        frame.state === "wifi" ? "到手机 WiFi 设置连接热点" :
        scene === "short" || scene === "adjust" ? "0.5 秒起每 0.2 秒加一步" :
        frame.elapsed >= 5300 ? "确认超时，热点未开启" : "满 2 秒后松开";
      host.querySelectorAll("[data-demo-phase]").forEach(item => {
        item.dataset.active = String(item.dataset.demoPhase === frame.phase);
        item.parentElement.classList.toggle("hidden", scene === "short" || scene === "adjust");
      });
      play.textContent = motion.matches ? "逐步演示" : running ? "暂停" : elapsed >= scenes[scene].duration ? "重播" : "播放演示";
      play.setAttribute("aria-pressed", String(running));
      if (announce) caption.textContent = frame.text;
    }
    function pause(announce = true) {
      if (running) elapsed = Math.min(performance.now() - started, scenes[scene].duration);
      running = false;
      cancelAnimationFrame(request);
      paint(announce);
    }
    function tick(now) {
      elapsed = Math.min(now - started, scenes[scene].duration);
      if (elapsed >= scenes[scene].duration) { running = false; paint(true); return; }
      paint(false);
      request = requestAnimationFrame(tick);
    }
    function next() {
      pause(false);
      elapsed = scenes[scene].steps.find(time => time > elapsed + 1) ?? scenes[scene].duration;
      paint(true);
    }
    play.addEventListener("click", () => {
      if (motion.matches) {
        if (elapsed >= scenes[scene].duration) elapsed = 0;
        else { next(); return; }
        paint(true);
        return;
      }
      if (running) { pause(); return; }
      if (elapsed >= scenes[scene].duration) elapsed = 0;
      running = true;
      started = performance.now() - elapsed;
      paint(true);
      request = requestAnimationFrame(tick);
    });
    step.addEventListener("click", next);
    host.querySelector("[data-demo-reset]").addEventListener("click", () => { pause(false); elapsed = 0; paint(true); });
    host.querySelectorAll("[data-demo-scene]").forEach(button => {
      button.addEventListener("click", () => {
        pause(false);
        scene = button.dataset.demoScene;
        elapsed = 0;
        host.querySelectorAll("[data-demo-scene]").forEach(item => item.setAttribute("aria-pressed", String(item === button)));
        host.querySelector("[data-demo-title]").textContent = scenes[scene].title;
        paint(true);
      });
    });
    document.addEventListener("visibilitychange", () => { if (document.hidden) pause(false); });
    host.closest("[data-operation-demos]")?.addEventListener("demo:pause", () => pause(false));
    window.addEventListener("blur", () => pause(false));
    motion.addEventListener("change", () => pause(false));
    if ("IntersectionObserver" in window) {
      new IntersectionObserver(entries => { if (!entries[0].isIntersecting) pause(false); }).observe(host);
    }
    paint(false);
  }
  async function init() {
    document.querySelectorAll("[data-button-demo]").forEach(initDemo);
    if (location.protocol === "file:") {
      document.querySelectorAll("[data-page-release]").forEach(el => el.textContent = "本地预览 · 未发布");
      return;
    }
    try {
      const response = await fetch("./site-info.json", { cache: "no-cache" });
      const info = response.ok ? await response.json() : null;
      const local = ["localhost", "127.0.0.1", ""].includes(location.hostname);
      const label = !info && !local ? "更新信息暂不可用" : releaseLabel(info);
      document.querySelectorAll("[data-page-release]").forEach(el => el.textContent = label);
    } catch (_) {
      document.querySelectorAll("[data-page-release]").forEach(el => {
        el.textContent = location.protocol === "file:" ? "本地预览 · 未发布" : "更新信息暂不可用";
      });
    }
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
  else init();
})();
