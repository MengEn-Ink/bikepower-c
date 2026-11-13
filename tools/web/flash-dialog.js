/* ESP Web Tools 10.4.0 DOM adapter: presentation only; no private installer state. */
(function (global) {
  'use strict';
  const SUCCESS_DELAY_MS = 2000;
  const COPY = new Map([
    ['Confirm Installation', '确认刷写'], ['Installation failed', '刷写失败'],
    ['Installation complete!', '刷写完成'], ['Erase User Data', '清除用户配置'],
    ['Erase device', '清除全部数据（谨慎选择）'], ['Preparing installation', '正在连接芯片并准备刷写…'],
    ['Logs & Console', '串口日志（排障用）'], ['Download Logs', '下载日志'], ['Reset Device', '重启开发板'],
    ['Wrapping up', '正在完成校验和重启，请稍候…'], ['Connecting', '正在连接开发板…'],
    ['Installing', '正在写入固件，请勿拔线'], ['Erasing', '正在清除数据，请勿拔线'],
    ['Install', '确认刷写'], ['Next', '下一步'], ['Back', '返回'], ['Close', '关闭'], ['Logs', '串口日志'],
    ['Error', '发生错误'], ['This will take', '预计需要'], ['2 minutes', '约 2 分钟'], ['a minute', '约 1 分钟'],
    ['Keep this page visible to prevent slow down', '请保持页面可见，不要拔线、刷新或关闭窗口'],
    ['All data on the device will be erased.', '开发板上的全部数据将被清除，包括配置和蓝牙绑定。'],
    ['All data on the device will be lost.', '开发板上的全部数据将丢失。'],
    ['Do you want to erase the device before installing', '安装前是否整片清除？不勾选也无法保留本全量固件覆盖范围内的配置和绑定。即将安装'],
    ['Do you want to install', '即将刷写'], ['Do you want to update to', '即将更新到'],
    ['Failed to initialize. Try resetting your device or holding the BOOT button while clicking INSTALL.', '无法连接芯片。请检查数据线和串口占用；按住 BOOT，再按一下 EN / RST，松开后返回重新刷写。'],
    ['Serial port is not readable/writable. Close any other application using it and try again.', '串口不可读写。请关闭串口监视器和其他占用程序，再关闭弹窗重试。'],
    ['Serial port is not ready. Close any other application using it and try again.', '串口未就绪。请检查数据线并关闭占用串口的软件。'],
    ['Failed to download manifest', '安装清单下载失败，请检查网络后重试'], ['Disconnected', '开发板已断开，请检查连接'],
  ]);
  function chinese(text) {
    if (typeof text !== 'string') return text;
    const trimmed = text.trim();
    if (COPY.has(trimmed)) return text.replace(trimmed, COPY.get(trimmed));
    if (/^(Install|Update) BikePower$/.test(trimmed)) return text.replace(trimmed, '安装 BikePower 固件');
    return text;
  }
  function attach(host, options = {}) {
    let stopped = false, timer = null, outcome = 'unknown', closing = false, lastStage = '';
    function report(name, text, progress) {
      const key = name + ':' + text;
      if (key === lastStage) return;
      lastStage = key;
      options.stage?.({ name, text, progress });
    }
    const observers = new Map();
    function stop() {
      stopped = true;
      clearTimeout(timer);
      for (const observer of observers.values()) observer.disconnect();
      observers.clear();
    }
    function closeCompleted() {
      if (stopped || closing || outcome !== 'success' || !host.isConnected) return;
      const dialog = host.shadowRoot?.querySelector('ew-dialog');
      const label = host.shadowRoot?.querySelector('ewt-page-message')?.label;
      if (!['Installation complete!', '刷写完成'].includes(label)) return;
      if (!dialog || typeof dialog.close !== 'function') return;
      closing = true;
      Promise.resolve(dialog.close()).catch(() => { closing = false; options.message?.('刷写已完成，但弹窗未能关闭，请手动点击关闭。'); });
    }
    function scanRoot(root) {
      if (!observers.has(root)) {
        const observer = new MutationObserver(scan);
        observer.observe(root, { childList: true, subtree: true, characterData: true, attributes: true });
        observers.set(root, observer);
      }
      const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
      let node;
      while ((node = walker.nextNode())) {
        if (node.parentElement?.closest('style, script, ewt-console, .bikepower-flash-helper, .bikepower-auto-close')) continue;
        const translated = chinese(node.nodeValue);
        if (translated !== node.nodeValue) node.nodeValue = translated;
      }
      for (const element of root.querySelectorAll('*')) {
        if (element.tagName === 'EWT-CONSOLE') continue;
        if (element.tagName === 'EWT-PAGE-MESSAGE' || element.tagName === 'EWT-PAGE-PROGRESS') {
          const label = element.label;
          const translated = chinese(label);
          if (translated !== label) element.label = translated;
        }
        if (element.tagName === 'EW-ICON-BUTTON' && !element.getAttribute('aria-label')) element.setAttribute('aria-label', '关闭安装弹窗');
        if (element.shadowRoot) scanRoot(element.shadowRoot);
      }
    }
    function scan() {
      if (stopped || !host.isConnected || !host.shadowRoot) return;
      for (const [root, observer] of observers) if (root.host && !root.host.isConnected) { observer.disconnect(); observers.delete(root); }
      const root = host.shadowRoot;
      const heading = root.querySelector('div[slot="headline"]')?.textContent.trim();
      const message = root.querySelector('ewt-page-message');
      const success = host.getAttribute('state') === 'INSTALL' && ['Installation complete!', '刷写完成'].includes(message?.label);
      const failed = host.getAttribute('state') === 'ERROR' || ['Installation failed', '刷写失败'].includes(heading);
      const progress = root.querySelector('ewt-page-progress');
      const label = typeof progress?.label === 'string' ? progress.label : progress?.shadowRoot?.textContent || '';
      const percent = Number.isFinite(progress?.progress) ? Math.max(0, Math.min(100, Math.round(progress.progress))) : null;
      if (failed) report('install-failed', '刷写失败；请关闭安装弹窗并等待串口释放后排障，不能重复点击刷写。');
      else if (success) report('installed', '安装组件报告刷写完成；正在关闭弹窗并释放串口。');
      else if (['Confirm Installation', '确认刷写', 'Erase User Data', '清除用户配置'].includes(heading)) report('confirm', '串口已连接、固件已校验。请在安装弹窗点击“确认刷写”；尚未写入。');
      else if (host.getAttribute('state') === 'LOGS') report('logs', '串口日志会话进行中；关闭安装弹窗并释放串口后才能再次刷写。');
      else if (progress) {
        if (/Wrapping up|完成校验和重启/.test(label)) report('wrapping', '正在完成安装校验和重启，请勿拔线或关闭页面。');
        else if (/Erasing|清除数据/.test(label)) report('erasing', '正在清除数据，请勿拔线或关闭页面。');
        else if (/Preparing installation|连接芯片|Connecting|连接开发板/.test(label)) report('connecting', '正在连接芯片并准备刷写；请勿拔线。');
        else if (percent !== null || /Installing|写入固件/.test(label)) report('writing', percent === null ? '正在写入固件，请勿拔线或关闭页面。' : '正在写入固件：' + percent + '%；请勿拔线或关闭页面。', percent);
        else report('dialog', '安装组件正在处理；请查看安装弹窗，当前无法确认写入进度。');
      } else report('dialog', '串口已连接、固件已校验。请在安装弹窗选择安装并确认刷写，不需再次选择串口。');
      if (failed) {
        outcome = 'failed'; clearTimeout(timer); timer = null;
        options.result?.(outcome);
        const dialog = root.querySelector('ew-dialog');
        if (dialog && !dialog.querySelector('[data-bp-failed-close]')) {
          const button = document.createElement('button');
          button.type = 'button'; button.slot = 'actions'; button.className = 'bikepower-auto-close';
          button.dataset.bpFailedClose = '1'; button.textContent = '关闭并排障';
          button.addEventListener('click', () => {
            if (outcome === 'failed') Promise.resolve(dialog.close()).catch(() => options.message?.('弹窗未能关闭，请手动关闭页面后排障。'));
          });
          dialog.appendChild(button);
        }
      } else if (success && outcome !== 'success') {
        outcome = 'success'; options.result?.(outcome);
        const dialog = root.querySelector('ew-dialog');
        const note = document.createElement('p');
        note.slot = 'content'; note.className = 'bikepower-auto-close';
        note.textContent = '刷写完成，2 秒后自动关闭弹窗并释放串口，再引导连接蓝牙。';
        dialog.appendChild(note);
        const button = document.createElement('button');
        button.type = 'button'; button.slot = 'actions'; button.className = 'bikepower-auto-close';
        button.textContent = '立即关闭'; button.addEventListener('click', closeCompleted);
        dialog.appendChild(button);
        timer = setTimeout(closeCompleted, SUCCESS_DELAY_MS);
      }
      scanRoot(root);
    }
    const ready = () => { if (!stopped) scan(); };
    if (host.updateComplete) Promise.resolve(host.updateComplete).then(ready);
    else queueMicrotask(ready);
    return { stop, outcome: () => outcome };
  }
  global.BikePowerFlashDialog = { attach, chinese };
})(window);
