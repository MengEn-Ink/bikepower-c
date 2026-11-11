const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('./web-flash/node_modules/playwright');
const root = path.join(__dirname, '..');
const catalog = JSON.parse(fs.readFileSync(path.join(root, 'tools/web/firmware-release-notes.json'), 'utf8'));
const item = { label: '通用 ESP32-C3', manifest: 'firmware/manifest.json', download: 'firmware/test.bin', filename: 'test.bin', size: 1238656, sha256: '1'.repeat(64) };
const releases = Object.entries(catalog.releases).map(([tag, changes], i) => ({tag, version:tag.slice(1), latest:i===0, published_at:'2026-11-11T08:30:30Z', boards:{'generic-c3':item}, changes, notes_full:`${tag} 原文\n${'x'.repeat(1100)}\n末尾升级说明`, notes:'截断旧摘要'}));
(async () => {
  const browser = await chromium.launch({headless:true, ...(process.env.BP_BROWSER_CHANNEL ? {channel:process.env.BP_BROWSER_CHANNEL} : {})});
  try {
    const page = await browser.newPage({viewport:{width:1280,height:900}});
    const errors=[];
    page.on('pageerror', e => errors.push(e.message));
    await page.addInitScript(() => {
      Object.defineProperty(navigator,'serial',{value:new EventTarget()});
      window.noteInjected=false;
    });
    let indexMissing=false;
    await page.route('**/*', route => {
      const url=new URL(route.request().url());
      if(url.hostname!=='example.test')return route.abort();
      if(url.pathname.endsWith('/firmware/latest.json'))return route.fulfill({json:{schema_version:1,available:true,...releases[0]}});
      if(url.pathname.endsWith('/firmware/releases.json'))return route.fulfill(indexMissing?{status:404,body:''}:{json:{schema_version:1,releases}});
      if(url.pathname.endsWith('.json'))return route.fulfill({json:{schema_version:1}});
      if(url.pathname==='/bikepower-c/')return route.fulfill({path:path.join(root,'tools/web/ble-monitor.html'),contentType:'text/html'});
      const name=path.basename(url.pathname);
      const file=path.join(root,'tools/web',name);
      if(/\.(js|css)$/.test(name)&&fs.existsSync(file))return route.fulfill({path:file,contentType:name.endsWith('.js')?'text/javascript':'text/css'});
      return route.fulfill({status:404,body:''});
    });
    await page.goto('https://example.test/bikepower-c/#/flash');
    await page.waitForFunction(()=>document.getElementById('fw-changes-title').textContent.includes('v4.4.1'));
    for(const theme of ['light','dark']) {
      await page.evaluate(theme=>document.documentElement.dataset.theme=theme,theme);
      for(const rel of releases) {
        await page.locator(`.fw-row[data-tag="${rel.tag}"]`).click();
        assert.equal(await page.locator('#fw-changes-title').textContent(),`${rel.tag} 升级内容`);
        assert.equal(await page.locator('.fw-change-summary').textContent(),rel.changes.summary);
        assert.equal(await page.locator('#fw-notes').textContent(),rel.notes_full,'full notes must not truncate after 900 characters');
        for(const key of ['added','fixed','notes']) assert.equal(await page.locator(`.fw-change-group[data-group="${key}"]`).isVisible(),true);
        assert.equal(await page.locator('#fw-changes').evaluate(el=>el.compareDocumentPosition(document.getElementById('flash-activate')) & Node.DOCUMENT_POSITION_FOLLOWING),4,'notes must precede install action');
        if(!rel.latest) {
          assert.equal(await page.locator('#install-button').isVisible(),false,'version notes do not bypass rollback acknowledgment');
          await page.locator('#fw-rollback-ack').check();
          assert.equal(await page.locator('#install-button').isVisible(),true);
        }
      }
      await page.evaluate(()=>closeCompactFlash());
      await page.setViewportSize({width:704,height:874});
      await page.locator('#mg-flash').click();
      assert.equal(await page.locator('#fw-changes').isVisible(),true);
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),true);
      await page.setViewportSize({width:1280,height:900});
      await page.evaluate(()=>selectFirmwareRelease(null));
      const shotDir=path.join(root,'tests/reports/pages-release-notes');
      fs.mkdirSync(shotDir,{recursive:true});
      await page.locator('#fw-changes').screenshot({path:path.join(shotDir,`${theme}.png`)});
    }
    await page.evaluate(()=>{
      renderFirmwareChanges({tag:'v99.0.0',release_url:'javascript:alert(1)',changes:{summary:'<img src=x onerror="noteInjected=true">',added:['<script>noteInjected=true</script>'],notes:['纯文字'],sources:['javascript:alert(1)','https://evil.test/','https://github.com@evil.test/MengEn-Ink/bikepower-c/']}});
    });
    assert.equal(await page.locator('#fw-changes img,#fw-changes script,#fw-changes a').count(),0,'untrusted notes cannot inject markup or unsafe links');
    assert.equal(await page.evaluate(()=>noteInjected),false);
    await page.evaluate(()=>renderFirmwareChanges({tag:'v99.0.0'}));
    assert.match(await page.locator('#fw-changes').textContent(),/尚未整理.*不会套用/);
    assert.match(await page.locator('[data-group="notes"]').textContent(),/覆盖 NVS/);
    indexMissing=true;
    await page.evaluate(async()=>await loadFirmwareCatalog());
    assert.equal(await page.locator('.fw-change-summary').textContent(),releases[0].changes.summary,'latest.json fallback keeps exact selected release notes');
    await page.evaluate(()=>{firmwareReleases=[];renderSelectedFirmware();});
    assert.equal(await page.locator('#fw-changes').isVisible(),false,'unavailable catalog hides stale version notes');
    assert.deepEqual(errors,[]);
    console.log('PASS: seven sourced versions, two themes, 704px compact layout, full original notes, rollback protection, safe text/URLs, unknown/latest-only/empty fallbacks; no hardware');
  } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
