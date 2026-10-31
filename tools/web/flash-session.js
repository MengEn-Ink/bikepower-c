/* ESP Web Tools 10.4.0：只使用 SerialPort、manifestPath、port 与 closed 边界。 */
(function (global) {
  'use strict';
  const DOWNLOAD_TIMEOUT_MS = 30000;
  const SESSION_NOTICE_MS = 180000;
  const MAX_FIRMWARE_BYTES = 4 * 1024 * 1024;
  const DIALOG_MODULE = 'https://unpkg.com/esp-web-tools@10.4.0/dist/web/install-dialog-im156JnI.js';

  async function fetchBytes(url, limit, signal) {
    const controller = new AbortController();
    const abort = () => controller.abort();
    signal.addEventListener('abort', abort, { once: true });
    const timer = setTimeout(abort, DOWNLOAD_TIMEOUT_MS);
    let reader;
    try {
      if (signal.aborted) throw new Error('会话已取消');
      const response = await fetch(url, { cache: 'no-store', signal: controller.signal });
      if (!response.ok) throw new Error('下载失败：HTTP ' + response.status);
      reader = response.body.getReader();
      const chunks = [];
      let size = 0;
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        size += value.length;
        if (size > limit) throw new Error('下载内容超过大小限制');
        chunks.push(value);
      }
      const bytes = new Uint8Array(size);
      let offset = 0;
      for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
      return bytes;
    } catch (error) {
      if (controller.signal.aborted) throw new Error('下载超时或会话已取消；未开始写入');
      throw error;
    } finally {
      clearTimeout(timer);
      signal.removeEventListener('abort', abort);
      if (reader) { await reader.cancel().catch(() => {}); reader.releaseLock(); }
    }
  }

  function siteURL(path) {
    if (typeof path !== 'string' || !/^firmware\/[A-Za-z0-9._+/-]+$/.test(path) || path.includes('..')) {
      throw new Error('固件索引路径无效');
    }
    const url = new URL(path, location.href);
    if (url.origin !== location.origin) throw new Error('固件必须同源');
    return url.href;
  }

  async function prepare(item, version, signal) {
    const manifestURL = siteURL(item.manifest);
    const binaryURL = siteURL(item.download);
    if (!Number.isInteger(item.size) || item.size <= 0 || item.size > MAX_FIRMWARE_BYTES || !/^[a-f0-9]{64}$/i.test(item.sha256)) {
      throw new Error('固件大小或 SHA256 索引无效');
    }
    const manifest = JSON.parse(new TextDecoder().decode(await fetchBytes(manifestURL, 65536, signal)));
    const build = manifest.builds && manifest.builds[0];
    const part = build && build.parts && build.parts[0];
    if (manifest.version !== version || manifest.builds.length !== 1 || build.chipFamily !== 'ESP32-C3' ||
        build.parts.length !== 1 || part.offset !== 0 || typeof part.path !== 'string' || new URL(part.path, manifestURL).href !== binaryURL) {
      throw new Error('manifest 与所选版本、ESP32-C3 或 0x0 merged bin 不一致');
    }
    const bytes = await fetchBytes(binaryURL, MAX_FIRMWARE_BYTES, signal);
    const digest = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), b => b.toString(16).padStart(2, '0')).join('');
    if (bytes.length !== item.size || digest !== item.sha256.toLowerCase()) throw new Error('固件大小或 SHA256 校验失败；已阻止写入');
    return { manifest, bytes };
  }

  function createSession(options) {
    let active = null;
    const notify = text => options.message(text);
    async function cleanup(session) {
      if (active !== session || session.cleaning) return;
      session.cleaning = true;
      session.controller.abort();
      clearTimeout(session.timer);
      if (session.disconnect) navigator.serial.removeEventListener('disconnect', session.disconnect);
      try {
        if (session.opened && (session.port.readable || session.port.writable)) await session.port.close();
        for (const url of session.urls) URL.revokeObjectURL(url);
        if (session.release) session.release();
        active = null;
        options.busy(false);
        notify('串口会话已关闭。以安装弹窗的最终结果为准；失败后请排障，再主动点击重试。');
      } catch (error) {
        session.cleaning = false;
        notify('串口资源尚未释放，已禁止重复刷写。请关闭占用终端/其他标签页，关闭本页后重新打开；不要继续点击安装。');
      }
    }
    async function start(item, version) {
      if (active) { notify('已有串口/安装/日志会话，请先返回并关闭安装弹窗。'); return; }
      if (!global.isSecureContext || !navigator.serial) { notify('刷写需要安全上下文和 Web Serial，请使用支持该 API 的桌面浏览器访问 HTTPS 页面。'); return; }
      if (!item) { notify('没有可刷写固件'); return; }
      const session = { controller: new AbortController(), urls: [], opened: false, cleaning: false };
      active = session;
      options.busy(true);
      notify('请选择开发板串口；授权后先下载并校验固件，尚未写入。');
      try {
        session.port = await navigator.serial.requestPort();
        if (navigator.locks) {
          await new Promise((resolve, reject) => {
            navigator.locks.request('bikepower-web-serial', { ifAvailable: true }, async lock => {
              if (!lock) { reject(new Error('另一标签页正在使用刷写/日志会话')); return; }
              const released = new Promise(done => { session.release = done; });
              resolve();
              await released;
            }).catch(reject);
          });
        }
        session.disconnect = event => {
          if (event.target !== session.port && event.port !== session.port) return;
          session.controller.abort();
          notify('开发板串口已断开；不要重试写入。等待安装弹窗返回/关闭后检查线材、供电和 BOOT。');
        };
        navigator.serial.addEventListener('disconnect', session.disconnect);
        const prepared = await prepare(item, version, session.controller.signal);
        notify('固件下载与 SHA256 校验通过；正在加载安装组件，尚未写入。');
        await options.loadDialog();
        if (session.controller.signal.aborted) throw new Error('设备已断开，未开始写入');
        const binary = URL.createObjectURL(new Blob([prepared.bytes]));
        session.urls.push(binary);
        prepared.manifest.builds[0].parts[0].path = binary;
        prepared.manifest.new_install_improv_wait_time = 0;
        const manifest = URL.createObjectURL(new Blob([JSON.stringify(prepared.manifest)], { type: 'application/json' }));
        session.urls.push(manifest);
        await session.port.open({ baudRate: 115200, bufferSize: 8192 });
        session.opened = true;
        const dialog = document.createElement('ewt-install-dialog');
        dialog.port = session.port;
        dialog.manifestPath = manifest;
        dialog.addEventListener('closed', () => { void cleanup(session); }, { once: true });
        session.dialog = dialog;
        options.beforeInstall();
        document.body.appendChild(dialog);
        notify('请在安装弹窗确认安装/清除数据。进度、失败和完成以该弹窗为准；本页不自动重刷。');
        session.timer = setTimeout(() => {
          if (active === session) notify('串口会话超过 3 分钟（可能正在查看日志/等待确认）。若进度卡住，请保持页面可见并检查安装弹窗；不强制中断、不自动重试。无法返回时关闭本页再排障。');
        }, SESSION_NOTICE_MS);
      } catch (error) {
        const cancelled = error.name === 'NotFoundError';
        await cleanup(session);
        if (active === session) return; // 清理失败必须保留禁止重试的提示。
        notify(cancelled ? '已取消串口选择，未开始写入。需要时再次点击即可。' : '未开始写入：' + error.message + '。关闭占用串口的软件，检查网络/线材后主动重试。');
      }
    }
    return { start, isBusy: () => active !== null };
  }

  function loadDialog() {
    let timer;
    return new Promise((resolve, reject) => {
      timer = setTimeout(() => reject(new Error('安装组件加载超时')), DOWNLOAD_TIMEOUT_MS);
      import(DIALOG_MODULE).then(() => {
        if (!customElements.get('ewt-install-dialog')) throw new Error('安装组件未注册');
        resolve();
      }).catch(reject);
    }).finally(() => clearTimeout(timer));
  }
  global.BikePowerFlash = { createSession, prepare, fetchBytes, loadDialog };
})(window);
