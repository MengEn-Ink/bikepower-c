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
      if (t >= release) { power = 180; text = "0.3 秒松开：短于 0.5 秒，减小一个步进（示例 20 W）。"; }
    } else {
      const hold = Math.min(t, release, 1999);
      if (hold >= 500) power += (Math.floor((hold - 500) / 200) + 1) * 20;
      if (t >= 500) text = "超过 0.5 秒：按住期间约每 0.2 秒增加一个步进。";
      if (scene === "adjust" && t >= release) text = "1.5 秒松开：保留调功结果，不进入配网确认。";
      if (scene !== "adjust" && t >= 2000) text = "已达到 2 秒：松开后才进入确认窗口。";
      if (scene !== "adjust" && t >= release) {
        power = 200;
        state = "confirm";
        text = "松开：本次临时调功回滚，LED 快闪；请在 3 秒内再按一次。";
      }
      if (scene === "wifi" && t >= 3300) {
        state = "wifi";
        text = "二次按下：打开 WiFi 热点。正常配网期间 BLE 继续工作。";
      }
      if (scene === "cancel" && t >= 5300) {
        state = "ble";
        text = "3 秒内未确认：取消配网，恢复蓝牙工作；功率保持按下前的值。";
      }
    }
    return { power, state, pressed, text, elapsed: t, progress: t / config.duration };
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
  if (typeof module !== "undefined") module.exports = { buttonFrame, releaseLabel, scenes };
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
      host.querySelector("[data-demo-power]").textContent = frame.power;
      host.querySelector("[data-demo-time]").textContent = (frame.elapsed / 1000).toFixed(1) + " s";
      host.querySelector("[data-demo-fill]").style.transform = "scaleX(" + frame.progress + ")";
      host.querySelector("[data-demo-state]").textContent = {
        ble: "蓝牙工作", confirm: "等待二次确认", wifi: "WiFi 配网"
      }[frame.state];
      host.querySelector("[data-demo-text]").textContent = frame.text;
      host.querySelector("[data-demo-key]").textContent = frame.pressed ? "按住" : "松开";
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
    motion.addEventListener("change", () => pause(false));
    if ("IntersectionObserver" in window) {
      new IntersectionObserver(entries => { if (!entries[0].isIntersecting) pause(false); }).observe(host);
    }
    paint(false);
  }
  async function init() {
    document.querySelectorAll("[data-button-demo]").forEach(initDemo);
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
