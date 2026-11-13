const { JSDOM } = require('./web-flash/node_modules/jsdom');
const fs = require('node:fs');
const assert = require('node:assert/strict');
const html = fs.readFileSync(require('node:path').join(__dirname, '../tools/web/ble-monitor.html'), 'utf8');
const dom = new JSDOM('<body></body>', { runScripts: 'outside-only' });
const w = dom.window;
const code = html.slice(html.indexOf('const ESP_DIALOG_SELECTOR'), html.indexOf('function uuidShort'));
w.eval(code);
const host = w.document.createElement('ewt-install-dialog');
host.setAttribute('state', 'INSTALL');
const root = host.attachShadow({ mode: 'open' });
root.innerHTML = '<ew-dialog><div slot="headline">Installation failed</div><ewt-page-message slot="content">Failed to initialize. Try resetting your device or holding the BOOT button while clicking INSTALL.</ewt-page-message></ew-dialog>';
w.document.body.append(host);
w.themeEspWebToolsDialog(host);
assert.doesNotMatch(root.textContent, /正在把 BikePower 固件写入开发板/, '初始化失败时不得残留进行中提示');
assert.doesNotMatch(root.textContent, /大约需要 2 分钟/);
console.log('PASS: initialization error DOM has no misleading writing helper');
dom.window.close();

(async () => {
  const d = new JSDOM('<body></body>', { runScripts: 'outside-only' });
  const w = d.window;
  w.eval(fs.readFileSync(require('node:path').join(__dirname, '../tools/web/flash-dialog.js'), 'utf8'));
  const host = w.document.createElement('ewt-install-dialog');
  host.setAttribute('state', 'INSTALL');
  const root = host.attachShadow({ mode: 'open' });
  w.document.body.append(host);
  const stages = [], results = [];
  const adapter = w.BikePowerFlashDialog.attach(host, { stage: state => stages.push(state), result: value => results.push(value) });
  const tick = () => new Promise(resolve => setImmediate(resolve));
  function show(heading, label, percentage) {
    root.innerHTML = '<ew-dialog><div slot="headline"></div></ew-dialog>';
    root.querySelector('div').textContent = heading;
    if (label !== undefined) {
      const el = w.document.createElement(percentage === 'success' ? 'ewt-page-message' : 'ewt-page-progress');
      el.label = label;
      if (typeof percentage === 'number') el.progress = percentage;
      if (typeof label !== 'string') { const shadow = el.attachShadow({ mode: 'open' }); shadow.textContent = 'Installing'; }
      root.querySelector('ew-dialog').append(el);
    }
  }
  show('Confirm Installation'); await tick(); assert.equal(stages.at(-1).name, 'confirm');
  show('Installing', 'Preparing installation'); await tick(); assert.equal(stages.at(-1).name, 'connecting');
  show('Installing', 'Erasing'); await tick(); assert.equal(stages.at(-1).name, 'erasing');
  show('Installing', {}, 42); await tick(); assert.equal(stages.at(-1).name, 'writing'); assert.equal(stages.at(-1).progress, 42);
  show('Installing', {}, 100); await tick(); assert.equal(stages.at(-1).name, 'writing'); assert.deepEqual(results, []);
  show('Installing', 'Wrapping up'); await tick(); assert.equal(stages.at(-1).name, 'wrapping');
  show('Installation failed'); await tick(); assert.equal(stages.at(-1).name, 'install-failed'); assert.equal(results.at(-1), 'failed');
  adapter.stop(); d.window.close();
  console.log('PASS: actual visible DOM drives confirmation, preparation, erase, numeric progress, wrapping, failure; 100% is not success');
})().catch(error => { console.error(error); process.exitCode = 1; });
