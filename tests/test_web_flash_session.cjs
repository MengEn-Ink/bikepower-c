const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { webcrypto, createHash } = require('node:crypto');
const { JSDOM } = require('./web-flash/node_modules/jsdom');
const source = fs.readFileSync(path.join(__dirname, '../tools/web/flash-session.js'), 'utf8');
const bytes = Buffer.from('offline merged bin');
const item = { manifest: 'firmware/manifest-generic-c3.json', download: 'firmware/bike.bin', size: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex') };
const manifest = { name: 'BikePower', version: '4.2.1', new_install_prompt_erase: true, builds: [{ chipFamily: 'ESP32-C3', parts: [{ offset: 0, path: 'bike.bin' }] }] };
const tick = () => new Promise(resolve => setImmediate(resolve));
function harness() {
  const dom = new JSDOM('<body></body>', { url: 'https://example.test/bikepower-c/', runScripts: 'outside-only' });
  const w = dom.window;
  const messages = [], busy = [], timers = new Map(), blobs = new Map();
  let seq = 0, picks = 0, opens = 0, closes = 0, before = 0;
  Object.assign(w, { isSecureContext: true, AbortController, TextDecoder, Blob });
  Object.defineProperty(w.crypto, 'subtle', { value: webcrypto.subtle });
  w.setTimeout = (fn, ms) => { const id = ++seq; timers.set(id, { fn, ms }); return id; };
  w.clearTimeout = id => timers.delete(id);
  w.URL.createObjectURL = blob => { const id = 'blob:https://example.test/' + ++seq; blobs.set(id, blob); return id; };
  w.URL.revokeObjectURL = id => blobs.delete(id);
  const port = {
    readable: null, writable: null, locked: false,
    async open(options) { opens++; assert.equal(options.baudRate, 115200); this.readable = {}; this.writable = {}; },
    async close() { closes++; if (this.locked) throw new Error('reader/writer locked'); this.readable = null; this.writable = null; },
  };
  const serial = new w.EventTarget();
  serial.requestPort = async () => { picks++; return port; };
  w.navigator.serial = serial;
  w.fetch = async url => new Response(url.endsWith('.json') ? JSON.stringify(manifest) : bytes);
  w.eval(source);
  w.customElements.define('ewt-install-dialog', class extends w.HTMLElement {});
  const options = { message: text => messages.push(text), busy: value => busy.push(value), beforeInstall: () => { before++; }, loadDialog: async () => {} };
  const session = w.BikePowerFlash.createSession(options);
  return { w, port, serial, options, session, messages, busy, timers, blobs,
    counts: () => ({ picks, opens, closes, before }),
    async close() { const dialog = w.document.querySelector('ewt-install-dialog'); dialog.dispatchEvent(new w.Event('closed')); dialog.remove(); await tick(); },
    dispose: () => dom.window.close(),
  };
}

test('verified immutable blobs; no installation before explicit upstream confirmation; close releases resources', async () => {
  const h = harness();
  await h.session.start(item, '4.2.1');
  assert.deepEqual(h.counts(), { picks: 1, opens: 1, closes: 0, before: 1 });
  const dialog = h.w.document.querySelector('ewt-install-dialog');
  assert.equal(dialog.port, h.port);
  const m = JSON.parse(await h.blobs.get(dialog.manifestPath).text());
  assert.equal(m.new_install_improv_wait_time, 0);
  assert.equal(m.new_install_prompt_erase, true);
  assert.deepEqual(Buffer.from(await h.blobs.get(m.builds[0].parts[0].path).arrayBuffer()), bytes);
  assert.equal(h.session.isBusy(), true);
  await h.close();
  assert.equal(h.blobs.size, 0);
  assert.equal(h.timers.size, 0);
  assert.equal(h.session.isBusy(), false);
  assert.deepEqual(h.busy, [true, false]);
  h.dispose();
});

for (const name of ['NotFoundError', 'SecurityError', 'NotAllowedError']) {
  test('chooser ' + name + ' does not open device or disconnect BLE', async () => {
    const h = harness();
    h.serial.requestPort = async () => { throw new h.w.DOMException('selection', name); };
    await h.session.start(item, '4.2.1');
    assert.equal(h.counts().opens, 0);
    assert.equal(h.counts().before, 0);
    assert.equal(h.session.isBusy(), false);
    if (name === 'NotFoundError') assert.match(h.messages.at(-1), /已取消/);
    h.dispose();
  });
}
for (const kind of ['insecure', 'unsupported', 'missing']) {
  test(kind + ' blocked before chooser', async () => {
    const h = harness();
    if (kind === 'insecure') h.w.isSecureContext = false;
    if (kind === 'unsupported') delete h.w.navigator.serial;
    await h.session.start(kind === 'missing' ? null : item, '4.2.1');
    assert.equal(h.counts().picks, 0);
    h.dispose();
  });
}
for (const [name, altered] of [
  ['digest', { sha256: '0'.repeat(64) }], ['size', { size: 12 }], ['too large', { size: 5000000 }],
  ['empty', { size: 0 }], ['cross origin', { download: 'https://other.test/bike.bin' }],
  ['traversal', { manifest: 'firmware/../manifest.json' }],
]) {
  test(name + ' fails preflight before port open', async () => {
    const h = harness();
    await h.session.start({ ...item, ...altered }, '4.2.1');
    assert.equal(h.counts().opens, 0);
    assert.equal(h.blobs.size, 0);
    assert.equal(h.session.isBusy(), false);
    assert.match(h.messages.at(-1), /未开始写入/);
    h.dispose();
  });
}
for (const [name, mutate] of [
  ['version', m => { m.version = '4.1.1'; }], ['offset', m => { m.builds[0].parts[0].offset = 4096; }],
  ['chip', m => { m.builds[0].chipFamily = 'ESP32-S3'; }], ['binary path', m => { m.builds[0].parts[0].path = 'other.bin'; }],
  ['parts', m => { m.builds[0].parts = []; }], ['builds', m => { delete m.builds; }],
]) {
  test('manifest ' + name + ' fails before port open', async () => {
    const h = harness(), m = structuredClone(manifest);
    mutate(m);
    h.w.fetch = async () => new Response(JSON.stringify(m));
    await h.session.start(item, '4.2.1');
    assert.equal(h.counts().opens, 0);
    assert.equal(h.session.isBusy(), false);
    h.dispose();
  });
}
for (const failure of ['404', 'HTML', 'network', 'module']) {
  test(failure + ' failure clears session and leaves device untouched', async () => {
    const h = harness();
    if (failure === 'module') h.options.loadDialog = async () => { throw new Error('CDN dependency failed'); };
    else h.w.fetch = async () => {
      if (failure === 'network') throw new Error('offline');
      return new Response(failure === 'HTML' ? '<html>blocked</html>' : '', { status: failure === '404' ? 404 : 200 });
    };
    await h.session.start(item, '4.2.1');
    assert.equal(h.counts().opens, 0);
    assert.equal(h.timers.size, 0);
    assert.equal(h.session.isBusy(), false);
    h.dispose();
  });
}

test('network timeout aborts actual request (not Promise.race)', async () => {
  const h = harness();
  let aborted = false;
  h.w.fetch = (url, options) => new Promise((resolve, reject) => {
    options.signal.addEventListener('abort', () => { aborted = true; reject(new Error('aborted')); });
  });
  const pending = h.session.start(item, '4.2.1');
  await tick();
  [...h.timers.values()].find(t => t.ms === 30000).fn();
  await pending;
  assert.equal(aborted, true);
  assert.equal(h.session.isBusy(), false);
  assert.equal(h.counts().opens, 0);
  h.dispose();
});

test('response body timeout aborts stalled streaming and releases reader', async () => {
  const h = harness();
  let cancelled = false;
  h.w.fetch = async (url, options) => {
    const stream = new ReadableStream({ start(controller) { options.signal.addEventListener('abort', () => controller.error(new Error('aborted'))); }, cancel() { cancelled = true; } });
    return new Response(stream);
  };
  const pending = h.session.start(item, '4.2.1');
  await tick();
  [...h.timers.values()].find(t => t.ms === 30000).fn();
  await pending;
  assert.equal(h.session.isBusy(), false);
  assert.match(h.messages.at(-1), /超时/);
  assert.equal(h.timers.size, 0);
  assert.equal(cancelled, false); // 已 error 的 stream 无需再执行底层 cancel
  h.dispose();
});

test('oversized streamed response cancels reader', async () => {
  const h = harness();
  let cancelled = false;
  h.w.fetch = async () => new Response(new ReadableStream({ start(c) { c.enqueue(new Uint8Array(65537)); }, cancel() { cancelled = true; } }));
  await h.session.start(item, '4.2.1');
  assert.equal(cancelled, true);
  assert.equal(h.counts().opens, 0);
  h.dispose();
});

test('busy port fails safely; explicit user retry only', async () => {
  const h = harness();
  h.port.open = async () => { throw new Error('port busy'); };
  await h.session.start(item, '4.2.1');
  assert.equal(h.session.isBusy(), false);
  assert.equal(h.blobs.size, 0);
  assert.match(h.messages.at(-1), /port busy/);
  h.dispose();
});

test('repeated click, logs, timeout warning and old callbacks cannot trigger reflash or release next session', async () => {
  const h = harness();
  await h.session.start(item, '4.2.1');
  const old = h.w.document.querySelector('ewt-install-dialog');
  old.setAttribute('state', 'LOGS');
  await h.session.start(item, '4.2.1');
  assert.equal(h.counts().picks, 1);
  [...h.timers.values()].find(t => t.ms === 180000).fn();
  assert.equal(h.session.isBusy(), true);
  assert.equal(h.counts().closes, 0);
  assert.doesNotMatch(h.messages.at(-1), /已完成|成功/);
  await h.close();
  await h.session.start(item, '4.2.1');
  old.dispatchEvent(new h.w.Event('closed'));
  assert.equal(h.session.isBusy(), true);
  assert.equal(h.counts().picks, 2);
  await h.close();
  h.dispose();
});

test('upstream reader/writer locks not released: fail closed and forbid retry', async () => {
  const h = harness();
  await h.session.start(item, '4.2.1');
  h.port.locked = true;
  await h.close();
  assert.equal(h.session.isBusy(), true);
  assert.match(h.messages.at(-1), /尚未释放/);
  await h.session.start(item, '4.2.1');
  assert.equal(h.counts().picks, 1);
  h.dispose();
});

test('selected-device disconnect warns but never auto-closes active write; unrelated disconnect ignored', async () => {
  const h = harness();
  await h.session.start(item, '4.2.1');
  const unrelated = new h.w.Event('disconnect');
  unrelated.port = {};
  h.serial.dispatchEvent(unrelated);
  const previous = h.messages.at(-1);
  const event = new h.w.Event('disconnect');
  event.port = h.port;
  h.serial.dispatchEvent(event);
  assert.notEqual(h.messages.at(-1), previous);
  assert.match(h.messages.at(-1), /已断开/);
  assert.equal(h.session.isBusy(), true);
  assert.equal(h.counts().closes, 0);
  await h.close();
  h.dispose();
});

test('cross-tab Web Lock excludes other sessions and releases only after closed', async () => {
  const h = harness();
  let held = false;
  h.w.navigator.locks = { async request(name, options, callback) {
    assert.equal(options.ifAvailable, true);
    if (held) return callback(null);
    held = true;
    try { await callback({ name }); } finally { held = false; }
  } };
  await h.session.start(item, '4.2.1');
  const other = h.w.BikePowerFlash.createSession(h.options);
  await other.start(item, '4.2.1');
  assert.equal(h.counts().opens, 1);
  assert.equal(other.isBusy(), false);
  assert.equal(held, true);
  await h.close();
  assert.equal(held, false);
  h.dispose();
});
