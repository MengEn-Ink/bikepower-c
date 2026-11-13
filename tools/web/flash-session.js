/* ESP Web Tools 10.4.0：只使用 SerialPort、manifestPath、port 与 closed 边界。 */
(function (global) {
  'use strict';
  const DOWNLOAD_TIMEOUT_MS = 30000;
  const PORT_TIMEOUT_MS = 15000;
  const SESSION_NOTICE_MS = 180000;
  const MAX_FIRMWARE_BYTES = 4 * 1024 * 1024;
  const DIALOG_MODULE = new URL('./vendor/esp-web-tools-10.4.0/install-dialog-im156JnI.js', document.currentScript?.src || location.href).href;
  const JOURNAL_KEY = 'bikepower.flash.pending.v1';
  const CACHE_TTL_MS = 10 * 60 * 1000;

  async function verifyBytes(bytes, item) {
    const digest = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), b => b.toString(16).padStart(2, '0')).join('');
    if (bytes.length !== item.size || digest !== item.sha256.toLowerCase()) throw new Error('固件大小或 SHA256 校验失败；已阻止写入');
  }
  function preparationKey(item, version) {
    if (!item || !Number.isInteger(item.size) || item.size <= 0 || item.size > MAX_FIRMWARE_BYTES || !/^[a-f0-9]{64}$/i.test(item.sha256)) throw new Error('固件大小或 SHA256 索引无效');
    return JSON.stringify([siteURL(item.manifest), siteURL(item.download), version, item.size, item.sha256.toLowerCase()]);
  }
  function createPreparationCache(options = {}) {
    let entry = null;
    const emit = value => options.change?.({ key: value.key, name: value.name, text: value.text });
    function status(item, version) {
      try {
        const key = preparationKey(item, version);
        return entry && entry.key === key && (entry.name !== 'ready' || Date.now() - entry.at < CACHE_TTL_MS)
          ? { key, name: entry.name, text: entry.text } : { key, name: 'idle', text: '固件尚未预下载' };
      } catch (error) { return { name: 'failed', text: error.message }; }
    }
    function select(item, version, retry = false) {
      const key = preparationKey(item, version);
      if (entry && entry.key === key && !(entry.name === 'ready' && Date.now() - entry.at >= CACHE_TTL_MS) && !(retry && entry.name === 'failed')) return entry;
      if (entry) entry.controller.abort();
      const value = { key, controller: new AbortController(), listeners: new Set(), name: 'manifest', text: '后台准备安装清单；不会连接串口或写入设备。' };
      entry = value;
      const report = (name, text) => {
        if (entry !== value) return;
        value.name = name; value.text = text;
        emit(value);
        for (const listener of value.listeners) listener(name, text);
      };
      value.promise = prepare({ ...item }, version, value.controller.signal, report).then(prepared => {
        if (value.controller.signal.aborted) throw new Error('预下载已取消');
        value.prepared = prepared; value.at = Date.now();
        report('ready', '固件已预下载并通过 SHA256 校验；连接串口后可直接进入安装确认。');
        return prepared;
      }).catch(error => {
        value.prepared = null;
        report('failed', '预下载未完成：' + error.message + '。点击刷写时可重试下载。');
        throw error;
      });
      value.promise.catch(() => {});
      return value;
    }
    function prefetch(item, version) {
      let promise;
      try { promise = select(item, version).promise.then(() => {}); }
      catch (error) { promise = Promise.reject(error); }
      promise.catch(() => {});
      return promise;
    }
    async function get(item, version, signal, report = () => {}) {
      if (signal.aborted) throw new Error('会话已取消');
      const value = select(item, version, true);
      const abort = () => value.controller.abort();
      signal.addEventListener('abort', abort, { once: true });
      value.listeners.add(report);
      report(value.name === 'ready' ? 'verify' : value.name, value.name === 'ready' ? '复用已预下载固件；刷写前再次校验 SHA256，尚未写入。' : value.text);
      try {
        const prepared = await value.promise;
        if (signal.aborted) throw new Error('会话已取消');
        const bytes = prepared.bytes.slice();
        await verifyBytes(bytes, item);
        if (signal.aborted) throw new Error('会话已取消');
        return { bytes, manifest: JSON.parse(JSON.stringify(prepared.manifest)) };
      } finally {
        value.listeners.delete(report);
        signal.removeEventListener('abort', abort);
      }
    }
    function clear() { if (entry) entry.controller.abort(); entry = null; }
    return { prefetch, get, status, clear };
  }

  async function fetchBytes(url, limit, signal, progress) {
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
        if (progress) progress(size);
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
    if (typeof path !== 'string' || !/^firmware\/[A-Za-z0-9._+/-]+$/.test(path) || path.includes('..')) throw new Error('固件索引路径无效');
    const url = new URL(path, location.href);
    if (url.origin !== location.origin) throw new Error('固件必须同源');
    return url.href;
  }

  async function prepare(item, version, signal, report = () => {}) {
    const manifestURL = siteURL(item.manifest);
    const binaryURL = siteURL(item.download);
    if (!Number.isInteger(item.size) || item.size <= 0 || item.size > MAX_FIRMWARE_BYTES || !/^[a-f0-9]{64}$/i.test(item.sha256)) throw new Error('固件大小或 SHA256 索引无效');
    report('manifest', '正在下载安装清单；尚未打开串口或写入。');
    const manifest = JSON.parse(new TextDecoder().decode(await fetchBytes(manifestURL, 65536, signal)));
    const build = Array.isArray(manifest.builds) && manifest.builds[0];
    const part = build && Array.isArray(build.parts) && build.parts[0];
    if (!build || !part || manifest.version !== version || manifest.builds.length !== 1 || build.chipFamily !== 'ESP32-C3' ||
        build.parts.length !== 1 || part.offset !== 0 || typeof part.path !== 'string' || new URL(part.path, manifestURL).href !== binaryURL) throw new Error('manifest 与所选版本、ESP32-C3 或 0x0 merged bin 不一致');
    report('download', '正在下载固件：0 / ' + item.size + ' 字节；尚未写入。');
    const bytes = await fetchBytes(binaryURL, MAX_FIRMWARE_BYTES, signal, count => report('download', '正在下载固件：' + count + ' / ' + item.size + ' 字节；尚未写入。'));
    report('verify', '正在校验固件大小和 SHA256；尚未写入。');
    await verifyBytes(bytes, item);
    if (signal.aborted) throw new Error('会话已取消');
    return { manifest, bytes };
  }

  function createSession(options) {
    let active = null;
    let snapshot = { name: 'idle', text: '', busy: false, canCancel: false };
    const notify = text => { snapshot = { ...snapshot, text }; options.message(text); };
    const journal = session => {
      try {
        session.record = JSON.stringify({ version: session.version, stage: session.stage, startedAt: session.startedAt });
        global.sessionStorage.setItem(JOURNAL_KEY, session.record);
      } catch (_) {}
    };
    function clearJournal(session) {
      try { if (global.sessionStorage.getItem(JOURNAL_KEY) === session.record) global.sessionStorage.removeItem(JOURNAL_KEY); } catch (_) {}
    }
    function stage(session, name, text) {
      if (active !== session) return;
      session.stage = name;
      journal(session);
      snapshot = { name, text, busy: true, version: session.version, canCancel: ['manifest', 'download', 'verify', 'component', 'ready'].includes(name) && !session.cleaning };
      options.stage?.({ ...snapshot });
      notify(text);
    }
    async function settledOperation(session, operation, message) {
      const timer = setTimeout(() => {
        if (active !== session) return;
        session.controller.abort();
        notify(message + '；底层操作仍在收尾，已禁止重试。请等待返回，不要再开一个刷写页面。');
      }, PORT_TIMEOUT_MS);
      try { return await operation(); } finally { clearTimeout(timer); }
    }
    async function cleanup(session) {
      if (active !== session || session.cleaning) return;
      session.cleaning = true;
      session.controller.abort();
      clearTimeout(session.timer);
      if (session.adapter) session.adapter.stop();
      if (session.disconnect) navigator.serial.removeEventListener('disconnect', session.disconnect);
      try {
        if (session.opened && (session.port.readable || session.port.writable)) {
          stage(session, 'cleanup', '正在释放串口；释放完成前不允许再次刷写。');
          await settledOperation(session, () => session.port.close(), '释放串口超时');
        }
        for (const url of session.urls) URL.revokeObjectURL(url);
        if (session.release) session.release();
        clearJournal(session);
        active = null;
        const outcome = session.result || 'unknown';
        const text = session.errorText || (outcome === 'success' ? '刷写完成，串口已释放。请等待设备启动，再点击连接蓝牙。' : outcome === 'failed' ? '刷写失败，串口已释放。请检查数据线、BOOT 和串口占用后主动重试。' : '安装弹窗已关闭，串口已释放；未确认刷写成功。需要时重新点击刷写。');
        snapshot = { name: session.cancelled ? 'cancelled' : session.errorText ? 'failed' : outcome === 'success' ? 'complete' : outcome === 'failed' ? 'failed' : 'closed', text, busy: false, canCancel: false, version: session.version, outcome };
        options.stage?.({ ...snapshot });
        options.busy(false);
        notify(text);
        if (session.dialog && options.onDialogClose) options.onDialogClose({ outcome: session.result || 'unknown', version: session.version });
        return true;
      } catch (error) {
        session.cleaning = false;
        stage(session, 'blocked', '串口资源尚未释放，已禁止重复刷写。请关闭占用终端/其他标签页，关闭本页后重新打开；不要继续点击安装。');
      }
    }
    async function start(item, version) {
      if (active) { notify('已有串口/安装/日志会话，请先返回并关闭安装弹窗。'); return; }
      if (!global.isSecureContext || !navigator.serial) { notify('刷写需要安全上下文和 Web Serial，请使用支持该 API 的桌面浏览器访问 HTTPS 页面。'); return; }
      if (!item) { notify('没有可刷写固件'); return; }
      const session = { controller: new AbortController(), urls: [], opened: false, cleaning: false, version, startedAt: Date.now() };
      active = session;
      options.busy(true);
      stage(session, 'picker', '请选择开发板串口；授权后先下载并校验固件，尚未写入。');
      try {
        session.port = await navigator.serial.requestPort();
        stage(session, 'lock', '串口已选择；正在检查本站其他刷写页面，尚未写入。');
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
          stage(session, 'disconnected', '开发板串口已断开；不要重试写入。等待安装弹窗返回/关闭后检查线材、供电和 BOOT。');
        };
        navigator.serial.addEventListener('disconnect', session.disconnect);
        const report = (name, text) => stage(session, name === 'ready' ? 'verify' : name, '串口已选择；' + text);
        const prepared = options.preparation ? await options.preparation.get(item, version, session.controller.signal, report) : await prepare(item, version, session.controller.signal, report);
        stage(session, 'component', '固件下载与 SHA256 校验通过；正在加载本站安装组件，尚未写入。');
        await options.loadDialog();
        if (session.controller.signal.aborted) throw new Error('会话已取消或设备已断开，未开始写入');
        const binary = URL.createObjectURL(new Blob([prepared.bytes]));
        session.urls.push(binary);
        prepared.manifest.builds[0].parts[0].path = binary;
        prepared.manifest.new_install_improv_wait_time = 0;
        const manifest = URL.createObjectURL(new Blob([JSON.stringify(prepared.manifest)], { type: 'application/json' }));
        session.urls.push(manifest);
        stage(session, 'opening', '正在打开已选择的串口（最多等待 15 秒后提示）；尚未写入。');
        await settledOperation(session, () => session.port.open({ baudRate: 115200, bufferSize: 8192 }), '打开串口超时');
        session.opened = true;
        if (session.controller.signal.aborted) throw new Error('串口操作超时或设备已断开，已阻止迟到安装');
        if (!session.port.readable || !session.port.writable) throw new Error('串口不可读写，未开始安装');
        const dialog = document.createElement('ewt-install-dialog');
        dialog.port = session.port;
        dialog.manifestPath = manifest;
        dialog.addEventListener('closed', () => { void cleanup(session); }, { once: true });
        if (global.BikePowerFlashDialog) session.adapter = global.BikePowerFlashDialog.attach(dialog, {
          message: notify,
          result: outcome => { if (active === session) session.result = outcome; },
          stage: state => { if (!session.cleaning && !session.controller.signal.aborted) stage(session, state.name, state.text); },
        });
        session.dialog = dialog;
        options.beforeInstall();
        document.body.appendChild(dialog);
        stage(session, 'dialog', '串口已打开，安装弹窗正在初始化。请在弹窗确认刷写；尚未确认时不会写入。本页不自动重刷。');
        session.timer = setTimeout(() => {
          if (active === session) notify('串口会话超过 3 分钟（可能正在查看日志/等待确认）。若进度卡住，请保持页面可见并检查安装弹窗；不强制中断、不自动重试。无法返回时关闭本页再排障。');
        }, SESSION_NOTICE_MS);
      } catch (error) {
        const cancelled = error.name === 'NotFoundError' && session.stage === 'picker';
        session.errorText = session.cancelled ? '已取消准备，未开始写入。需要时重新选择串口。' : cancelled ? '已取消串口选择，未开始写入。需要时再次点击即可。' : '未开始写入：' + error.message + '。关闭占用串口的软件，检查网络/线材后主动重试。';
        if (cancelled) session.cancelled = true;
        await cleanup(session);
      }
    }
    function canCancel() { return !!active && ['manifest', 'download', 'verify', 'component'].includes(active.stage) && !active.cleaning; }
    async function cancel() {
      if (!canCancel()) { if (active) notify('当前串口或安装组件仍在工作，请在安装弹窗安全返回/关闭；不强制中断、不自动重试。'); return; }
      active.cancelled = true;
      active.controller.abort();
      notify('正在取消准备；等待下载/组件操作收尾后释放会话，尚未写入。');
    }
    function recoveryNotice() {
      try {
        const value = JSON.parse(global.sessionStorage.getItem(JOURNAL_KEY));
        if (value && typeof value.stage === 'string') return '上次串口会话被刷新或关闭中断，不能确认刷写成功。本站不会自动续写；先确认开发板状态，再主动重新选择串口。全量安装会覆盖配置和绑定。';
      } catch (_) {}
      return '';
    }
    return { start, cancel, canCancel, recoveryNotice, isBusy: () => active !== null, getState: () => ({ ...snapshot, busy: active !== null }) };
  }

  let dialogReady;
  function loadDialog() {
    if (customElements.get('ewt-install-dialog')) return Promise.resolve();
    if (dialogReady) return dialogReady;
    let timer;
    dialogReady = new Promise((resolve, reject) => {
      timer = setTimeout(() => reject(new Error('本站安装组件加载超时；未开始写入')), DOWNLOAD_TIMEOUT_MS);
      import(DIALOG_MODULE).then(() => {
        if (!customElements.get('ewt-install-dialog')) throw new Error('安装组件未注册');
        resolve();
      }).catch(reject);
    }).finally(() => clearTimeout(timer));
    dialogReady.catch(() => { dialogReady = null; });
    return dialogReady;
  }
  global.BikePowerFlash = { createSession, createPreparationCache, prepare, fetchBytes, loadDialog };
})(window);
