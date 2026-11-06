/* 操作教学只使用独立内存状态，事件不进入真实设备会话。 */
(function () {
  "use strict";
  const boardSource = "https://wiki.luatos.com/chips/esp32c3/board.html";
  let boardSequence = 0;
  const clamp = value => Math.max(0, Math.min(2000, Math.round(Number(value) || 0)));
  function practice(previous, event, time) {
    const s = { power: 200, baseline: 200, mode: "ble", pressed: false, start: 0, deadline: 0, ...previous };
    const now = Math.max(0, Number(time) || 0);
    if (s.mode === "confirm" && now >= s.deadline) {
      s.mode = "ble";
      s.deadline = 0;
    }
    if (event === "reset") return practice(null, "tick", now);
    if (event === "cancel") {
      if (s.pressed) s.power = s.baseline;
      s.pressed = false;
      s.mode = "ble";
      s.deadline = 0;
      return s;
    }
    if (event === "down" && !s.pressed) {
      if (s.mode === "confirm") { s.mode = "wifi"; s.deadline = 0; }
      if (s.mode === "ble") { s.baseline = s.power; s.start = now; }
      s.pressed = true;
    }
    if (s.pressed && s.mode === "ble") {
      const hold = Math.min(Math.max(0, now - s.start), 1999);
      s.power = clamp(s.baseline + (hold >= 500 ? (Math.floor((hold - 500) / 200) + 1) * 20 : 0));
      if (event === "up") {
        if (now - s.start >= 2000) {
          s.power = s.baseline;
          s.mode = "confirm";
          s.deadline = now + 3000;
        } else if (now - s.start < 500) s.power = clamp(s.baseline - 20);
      }
    }
    if (event === "up") s.pressed = false;
    return s;
  }
  function configuration(previous, event, data = {}) {
    const s = { applied: 200, draft: 200, status: "ready", ...previous };
    if (event === "edit") { s.draft = clamp(data.power); s.status = "draft"; }
    if (event === "fail") s.status = "error";
    if (event === "save") { s.applied = s.draft; s.status = "saved"; }
    if (event === "reset") return configuration(null, "tick");
    return s;
  }
  function correctedPower(input, mode, amount) {
    const raw = clamp(input);
    if (!raw) return 0;
    return clamp(mode === "offset" ? raw + Number(amount || 0) :
      mode === "percent" ? raw * (1 + Number(amount || 0) / 100) : raw);
  }
  function handoff(previous, event) {
    const s = { owner: "web", status: "ready", ...previous };
    if (event === "release") { if (s.owner === "web") s.owner = "none"; s.status = "released"; }
    if (event === "web" || event === "app") {
      if (s.owner !== "none" && s.owner !== event) s.status = "occupied";
      else { s.owner = event; s.status = "connected"; }
    }
    if (event === "reset") return handoff(null, "tick");
    return s;
  }
  function boardMarkup() {
    const halo = "operation-led-" + (++boardSequence);
    const pads = Array.from({ length: 16 }, (_, i) => {
      const y = 84 + i * 18;
      return `<path d="M76 ${y}h10a5 5 0 0 1 0 10H76M224 ${y}h-10a5 5 0 0 0 0 10h10" fill="#d8bb77"/><circle cx="81" cy="${y + 5}" r="2.7" fill="#1a405c"/><circle cx="219" cy="${y + 5}" r="2.7" fill="#1a405c"/>`;
    }).join("");
    return `<svg class="luatos-board" viewBox="0 0 320 440" role="img" aria-label="设备正面示意：左侧复位键，右侧操作键，底部供电，右下指示灯">
      <defs><radialGradient id="${halo}"><stop stop-color="#fff5e5"/><stop offset=".16" stop-color="#ffb7a0" stop-opacity=".95"/><stop offset=".42" stop-color="#ff5940" stop-opacity=".65"/><stop offset="1" stop-color="#ff5940" stop-opacity="0"/></radialGradient></defs>
      <rect x="76" y="25" width="148" height="359" rx="5" fill="#0865a0" stroke="#084a79" stroke-width="2"/>
      <path d="M98 80V35h18v16h17V35h17v16h17V35h25v44" fill="none" stroke="#83b9d8" stroke-width="5"/>
      <path d="M105 163v45l28 20v64m64-129v45l-22 20v65m-56-180H96v133m89-133h18v133" fill="none" stroke="#64a4ca" stroke-width="1.5"/>
      ${pads}
      <g fill="#ebd6aa"><path d="M118 129h66v4h-66m0 58h66v4h-66"/><path d="M115 137v48h4v-48m66 0v48h4v-48"/></g>
      <rect x="120" y="136" width="62" height="52" rx="2" fill="#25292e" stroke="#d0d4d6"/>
      <text x="151" y="163" text-anchor="middle" fill="#b8c1c7" font-size="9">BikePower</text>
      <rect x="183" y="195" width="14" height="25" rx="2" fill="#d8d6c6"/>
      <rect x="126" y="224" width="50" height="33" fill="#232930" stroke="#c1c7cc"/>
      <g fill="#d0c1a0"><path d="M122 225v30m58-30v30" stroke="#d0c1a0" stroke-width="4"/></g>
      <g fill="#d3dde3" font-size="8"><text x="96" y="276">RST</text><text x="176" y="276">BOOT</text></g>
      <g><rect x="96" y="282" width="26" height="29" rx="2" fill="#a5a9a3"/><circle cx="109" cy="296" r="9" fill="#ece3c6" stroke="#7c7768"/></g>
      <g class="board-boot"><rect x="177" y="282" width="26" height="29" rx="2" fill="#a5a9a3"/><circle cx="190" cy="296" r="9" fill="#ece3c6" stroke="#7c7768"/></g>
      <circle class="board-press-ring" cx="190" cy="296" r="22" fill="none" stroke="#f5c451" stroke-width="3"/>
      <g fill="#232930" stroke="#b0bac2"><rect x="132" y="285" width="12" height="17"/><rect x="150" y="285" width="16" height="17"/><rect x="101" y="315" width="10" height="18"/></g>
      <rect x="98" y="354" width="8" height="15" rx="1" fill="#dae2e5"/>
      <rect x="194" y="354" width="8" height="15" rx="1" fill="#e6e6df"/><rect data-board-led x="195" y="357" width="6" height="9" fill="#edf1f3"/>
      <rect x="120" y="346" width="60" height="47" rx="4" fill="#b8b7aa" stroke="#f1e9cd" stroke-width="2"/>
      <rect x="125" y="357" width="50" height="24" rx="8" fill="#243238" stroke="#e3ddc4" stroke-width="2"/>
      <rect x="136" y="367" width="28" height="4" rx="1" fill="#969c98"/>
      <circle data-led-halo cx="198" cy="361" r="80" fill="url(#${halo})" pointer-events="none"/>
      <rect class="board-led-core" x="195" y="357" width="6" height="9" rx="2" fill="#fff8ed"/>
      <g class="board-callouts" fill="none" stroke="currentColor" stroke-width="1"><path d="M190 296h51v-26h15M109 296H44m154 66h40v28h17"/></g>
      <g class="board-labels" fill="currentColor" font-size="14"><text x="240" y="259">操作键</text><text x="9" y="287">复位</text><text x="242" y="410">指示灯</text><text x="150" y="432" text-anchor="middle" font-size="12">底部接电</text></g>
    </svg>`;
  }
  function buttonMarkup() {
    return `<div class="button-demo" data-button-demo data-state="ble" data-pressed="true" data-playing="false">
      <div class="demo-scenes" role="group" aria-label="选择按钮操作演示">
        <button type="button" data-demo-scene="short" aria-pressed="false">短按减功率</button>
        <button type="button" data-demo-scene="adjust" aria-pressed="false">按住持续调功</button>
        <button type="button" data-demo-scene="wifi" aria-pressed="true">长按 + 二次确认</button>
        <button type="button" data-demo-scene="cancel" aria-pressed="false">不确认，自动取消</button>
      </div>
      <div class="demo-head"><b data-demo-title>长按 + 二次确认</b><time data-demo-time>0.0 s</time></div>
      <div class="demo-stage"><div class="demo-light-field" aria-hidden="true"></div><div class="demo-board">${boardMarkup()}<span class="demo-caption" data-demo-key>按住</span></div>
        <div class="demo-metric">示例目标功率<strong><span data-demo-power>200</span><small>W</small></strong>
          <span class="demo-state" data-demo-state>蓝牙工作</span><span class="demo-countdown" data-demo-countdown>满 2 秒后松开</span>
          <ol class="demo-sequence"><li data-demo-phase="hold">按住 2 秒</li><li data-demo-phase="release">松开，看快闪</li><li data-demo-phase="confirm">3 秒内再按</li></ol>
          <span class="demo-led-label" data-demo-led-label>指示灯 · 慢闪</span>
        </div>
      </div>
      <div class="demo-track" aria-hidden="true"><i data-demo-fill></i></div>
      <p class="demo-text" data-demo-text></p>
      <div class="demo-controls"><button type="button" class="btn primary" data-demo-play aria-pressed="false">播放演示</button><button type="button" class="btn" data-demo-step>下一步</button><button type="button" class="btn ghost" data-demo-reset>重置</button></div>
      <span class="demo-caption" data-demo-caption role="status" aria-live="polite"></span>
    </div>`;
  }
  function suiteMarkup() {
    return `<nav class="operation-tabs" aria-label="操作演示场景">${[
      ["button", "按钮配网"], ["save", "保存生效"], ["bridge", "功率校正"], ["handoff", "连接交接"]
    ].map(([key, title], i) => `<button type="button" data-operation-tab="${key}" aria-pressed="${i === 0}">${title}</button>`).join("")}</nav>
      <p class="operation-disclaimer">模拟演示 · 可以放心练习</p>
      <section data-operation-panel="button">${buttonMarkup()}
        <details class="practice"><summary>亲手按住，练一次</summary><div class="practice-body">
          <p>按住下方按钮 2 秒，松开，再按一次。键盘使用空格或 Enter。</p>
          <div class="practice-row"><button type="button" class="btn primary practice-key" data-practice-key>按住练习</button><output data-practice-output>200 W · 蓝牙工作</output><button type="button" class="btn ghost" data-practice-reset>重新练习</button></div>
          <p data-practice-note role="status" aria-live="polite">初始目标 200 W。短按减 20 W，0.5 秒起持续增加。</p>
        </div></details>
        <p class="operation-caution"><b>注意</b>先松开再确认；别按左侧复位键。灯色以你的设置为准。</p>
      </section>
      <section data-operation-panel="save" class="hidden">
        <h3>保存后，数值才生效。</h3>
        <div class="save-diagram"><label>草稿目标 <span class="operation-field"><input data-config-input type="number" min="0" max="2000" step="20" value="200" inputmode="numeric"><span>W</span></span></label>
          <span class="operation-arrow" aria-hidden="true">→</span><div class="save-applied">当前示例输出<strong><output data-config-applied>200</output><small>W</small></strong><span>保存成功后更新</span></div></div>
        <p class="operation-feedback" data-config-status role="status" aria-live="polite">修改草稿，观察右侧输出保持不变。</p>
        <div class="operation-controls"><button class="btn primary" type="button" data-config-save>保存示例</button><button class="btn" type="button" data-config-fail>模拟保存失败</button><button class="btn ghost" type="button" data-config-reset>重置</button></div>
        <div class="story-controls"><button type="button" class="btn" data-story-play="save">播放操作</button><button type="button" class="btn ghost" data-story-next="save">下一步</button><span data-story-state="save">编辑 → 失败保留 → 重试成功</span></div>
        <p class="operation-caution"><b>注意</b>看到“保存成功”再退出；失败时保留页面重试。</p>
      </section>
      <section data-operation-panel="bridge" class="hidden">
        <h3>调一下，看输出如何变化。</h3>
        <div class="bridge-path"><div>真实功率计<strong><output data-bridge-raw>200</output><small>W</small></strong></div><span class="operation-arrow" aria-hidden="true">→</span><div>BikePower 校正<strong><output data-bridge-result>220</output><small>W</small></strong></div><span class="operation-arrow" aria-hidden="true">→</span><div>骑行 App<strong data-bridge-app>220 W</strong></div></div>
        <div class="operation-fields"><label>原始功率 / W<input type="number" min="0" max="2000" value="200" data-bridge-input inputmode="numeric"></label>
          <label>校正方式<select data-bridge-mode><option value="offset">固定偏移 / W</option><option value="percent">比例增加 / %</option><option value="raw">不校正</option></select></label>
          <label>校正量<input type="number" min="-200" max="200" value="20" data-bridge-amount inputmode="decimal"></label></div>
        <p class="operation-feedback" data-bridge-status role="status" aria-live="polite">200 W + 20 W = 220 W</p>
        <div class="operation-controls"><button type="button" class="btn" data-bridge-zero>模拟停踩 · 0 W</button><button type="button" class="btn ghost" data-bridge-reset>恢复 200 W</button></div>
        <div class="story-controls"><button type="button" class="btn" data-story-play="bridge">播放操作</button><button type="button" class="btn ghost" data-story-next="bridge">下一步</button><span data-story-state="bridge">固定偏移 → 比例增加 → 停踩归零</span></div>
        <p class="operation-caution"><b>注意</b>停踩保持 0 W；真实来源的心率默认关闭，开启后仍是模拟值。</p>
      </section>
      <section data-operation-panel="handoff" class="hidden">
        <h3>先断开网页，再连接 App。</h3>
        <div class="handoff-diagram" data-handoff-owner="web"><div data-owner-node="web">网页<span data-owner-web>已连接</span></div><svg viewBox="0 0 100 30" aria-hidden="true"><path d="M0 15h100" stroke="currentColor" stroke-width="2" stroke-dasharray="5 5"/></svg><div data-owner-node="board">BikePower<span>1 个下游连接</span></div><svg viewBox="0 0 100 30" aria-hidden="true"><path d="M0 15h100" stroke="currentColor" stroke-width="2" stroke-dasharray="5 5"/></svg><div data-owner-node="app">骑行 App<span data-owner-app>等待连接</span></div></div>
        <p class="operation-feedback" data-handoff-status role="status" aria-live="polite">网页正在占用连接，先释放后再去 App。</p>
        <div class="operation-controls"><button type="button" class="btn primary" data-handoff-release>演示释放网页连接</button><button type="button" class="btn" data-handoff-app>演示 App 配对</button><button type="button" class="btn ghost" data-handoff-reset>重置</button></div>
        <div class="story-controls"><button type="button" class="btn" data-story-play="handoff">播放操作</button><button type="button" class="btn ghost" data-story-next="handoff">下一步</button><span data-story-state="handoff">占用失败 → 网页释放 → App 连接</span></div>
        <p class="operation-caution"><b>注意</b>设备一次只连接一个 App 或网页。实际交接请点连接详情中的“释放网页连接”。</p>
      </section>`;
  }
  const guides = {
    install: { title: "电脑安装", device: "computer", labels: ["设备", "安装进度"], frames: [
      ["接好数据线，选择你的设备。", 0, ["BikePower · 已选择", "等待安装"], "连接串口并刷写", "ready"],
      ["按弹窗确认，等待写入。", 1, ["BikePower · 已连接", "正在安装 60%"], "正在安装", "working"],
      ["看到完成提示，关闭弹窗后重新接电。", 1, ["BikePower · 已安装", "Installation complete!"], "关闭", "success"]
    ], note: "电脑 Chrome / Edge + 数据线；安装中别拔线。擦除会清掉配置。" },
    connect: { title: "连接 App", labels: ["设备", "功率 / 踏频 / 心率"], frames: [
      ["给设备接电，打开 App 添加设备。", 0, ["BikePower · 等待连接", "— / — / —"], "搜索功率计", "ready"],
      ["在搜索结果中选择 BikePower。", 0, ["BikePower · 已选择", "等待数据"], "连接", "working"],
      ["读数开始更新，就可以用了。", 1, ["BikePower · 已连接", "200 W / 80 rpm / 135 bpm"], "已连接", "success"]
    ], note: "搜不到时，先断开其他 App 或网页；按对应传感器类型搜索。" },
    hotspot: { title: "手机连接热点", labels: ["无线网络", "设备配置页"], frames: [
      ["打开手机 WiFi，选择设备热点。", 0, ["BikePower-XXXXXX", "等待连接"], "连接", "ready"],
      ["输入密码 12345678，保持连接。", 0, ["已连接 · 无互联网", "正在打开"], "保持连接", "working"],
      ["自动弹出配置页；没弹出就手动打开。", 1, ["BikePower-XXXXXX · 已连接", "http://192.168.4.1"], "进入配置", "success"]
    ], note: "“无互联网”正常；打不开时在浏览器输入 192.168.4.1。" },
    cycling: { title: "设置骑行", labels: ["训练模式", "目标功率"], frames: [
      ["想手动设定瓦数，选固定模式。", 0, ["固定模式", "200 W"], "编辑", "ready"],
      ["修改目标，或选择配速机器人。", 1, ["固定模式", "260 W · 未保存"], "保存并应用", "working"],
      ["保存成功，再到 App 核对读数。", 1, ["固定模式", "260 W · 已生效"], "保存成功", "success"]
    ], note: "App 控制训练强度时，手动目标可能暂不生效。" },
    running: { title: "设置跑步", labels: ["运动类型", "速度 / 距离 / 用时"], frames: [
      ["选择跑步，设置速度或配速。", 0, ["跑步 · 未保存", "10 km/h"], "保存并切换", "ready"],
      ["保存后重新配对，开始累计。", 1, ["跑步 · 已连接", "10 km/h / 20 m / 7 s"], "暂停", "success"],
      ["暂停后归零，距离和用时保持。", 1, ["跑步 · 已暂停", "0 km/h / 20 m / 7 s"], "恢复", "ready"],
      ["恢复后，继续累计。", 1, ["跑步 · 运行中", "10 km/h / 25 m / 9 s"], "暂停", "success"]
    ], note: "需要支持跑步传感器的 App；重置清空距离和用时，暂停状态不变。" },
    profile: { title: "填写用户资料", labels: ["体重", "训练基准功率"], frames: [
      ["打开用户资料。", 0, ["70 kg", "200 W"], "编辑", "ready"],
      ["按自己的情况填写。", 0, ["75 kg · 未保存", "200 W"], "保存并应用", "working"],
      ["保存后，强度计算使用新资料。", 0, ["75 kg · 已保存", "200 W"], "保存成功", "success"]
    ], note: "体重影响配速机器人强度；不确定的参数先保留默认。" },
    device: { title: "修改名称", labels: ["设备名称", "App 搜索结果"], frames: [
      ["打开设备设置，修改名称。", 0, ["BikePower", "BikePower"], "编辑", "ready"],
      ["填写容易认出的名字，再保存。", 0, ["我的训练伙伴 · 未保存", "BikePower"], "保存并应用", "working"],
      ["重新搜索，就能看到新名称。", 1, ["我的训练伙伴 · 已保存", "我的训练伙伴"], "保存成功", "success"]
    ], note: "修改名称或运动类型后，可能需要删除旧配对再搜索。" },
    step: { title: "调整按钮步进", labels: ["每次调节", "短按后的目标"], frames: [
      ["打开设备设置，找到调功步进。", 0, ["20 W", "200 W"], "编辑", "ready"],
      ["改为 10 W，保存。", 0, ["10 W · 已保存", "200 W"], "短按一次", "working"],
      ["短按后，目标从 200 降到 190 W。", 1, ["10 W", "190 W"], "已减少 10 W", "success"]
    ], note: "可设 5–50 W；此演示使用固定骑行模式。" },
    lights: { title: "看懂指示灯", labels: ["当前状态", "灯光节奏"], frames: [
      ["慢闪：等待 App 连接。", 1, ["等待连接", "慢闪"], "打开 App", "ready"],
      ["长按松开后快闪：等待再次确认。", 1, ["等待确认", "快闪 · 3 秒内再按"], "再按一次", "working"],
      ["连接后显示连接灯效。", 1, ["已连接", "连接灯效"], "已连接", "success"]
    ], note: "灯色和节奏可自定义；静默模式可能不亮，以连接状态为准。" },
    upstream: { title: "接入功率计", labels: ["功率来源", "连接测试"], frames: [
      ["先转动曲柄唤醒，再开始扫描。", 0, ["功率计 · 已发现", "等待测试"], "选择并测试", "ready"],
      ["选择设备，等待实际功率数据。", 1, ["功率计 · 已选择", "200 W → 202 W"], "测试成功", "working"],
      ["测试成功后保存，再核对 App 输出。", 1, ["真实功率计 · 已保存", "原始 200 W → 输出 220 W"], "保存成功", "success"]
    ], note: "测试不等于保存。断线可能回退模拟，请查看实际来源；最多保存三台，共享校正。" },
    about: { title: "核对设备版本", labels: ["当前设备", "安装版本"], frames: [
      ["展开设备信息。", 0, ["BikePower", "等待读取"], "查看设备", "ready"],
      ["查看安装版本。", 1, ["BikePower · 已读取", "示例版本"], "核对版本", "working"],
      ["升级后，再确认设备显示的新版本。", 1, ["BikePower · 已重新连接", "新版 · 示例"], "已核对", "success"]
    ], note: "页面更新不会自动升级设备。功能以已安装版本为准。" },
    exit: { title: "保存并退出", labels: ["修改内容", "设备热点"], frames: [
      ["修改完成，先保存。", 0, ["260 W · 未保存", "已开启"], "保存并应用", "ready"],
      ["等到保存成功，再关闭热点。", 0, ["260 W · 已保存", "已开启"], "关闭 WiFi", "success"],
      ["确认关闭，回到 App。", 1, ["260 W · 已保存", "已关闭"], "返回 App", "success"]
    ], note: "关闭热点不会保存草稿。默认 5 分钟；需要更多时间请点续期。" },
    troubleshoot: { title: "搜不到 / 连不上", labels: ["网页连接", "App 连接"], frames: [
      ["连不上？先看看是不是网页在占用。", 0, ["已连接", "连接失败"], "检查连接", "error"],
      ["释放网页连接，并关闭其他占用。", 0, ["已释放", "等待连接"], "重新搜索", "working"],
      ["按正确的设备类型重新配对。", 1, ["保持断开", "BikePower · 已连接"], "已连接", "success"]
    ], note: "仍无数据：检查供电、运动类型和 App 支持；热点问题看“手机连接热点”。" },
    overview: { title: "记住使用顺序", labels: ["配置", "App"], frames: [
      ["连接热点，打开配置页。", 0, ["已打开", "保持当前状态"], "修改设置", "ready"],
      ["保存成功，再关闭热点。", 0, ["已保存 · 已关闭热点", "等待配对"], "返回 App", "working"],
      ["连接 App，确认读数。", 1, ["已生效", "200 W / 80 rpm"], "开始使用", "success"]
    ], note: "未预装的设备先用电脑安装；每次修改都要等保存成功。" },
    compare: { title: "复制名称与设备信息", device: "computer", labels: ["参考设备", "BikePower"], frames: [
      ["电脑打开设备比对，添加两台设备。", 0, ["已读取", "已连接"], "查看差异", "ready"],
      ["选择要复制的名称与设备信息。", 0, ["名称与设备信息", "等待保存"], "写入 BikePower", "working"],
      ["保存后重新配对，再实际检查。", 1, ["保持不变", "已保存 · 回读一致"], "释放连接", "success"]
    ], note: "复制信息不保证兼容。测试 App 前释放网页连接；可导出诊断报告。" }
  };
  function guideScreen(name, position) {
    const guide = guides[name], frame = guide.frames[position];
    if (name === "lights") return `<div class="guide-lights-board">${boardMarkup()}</div><div class="guide-light-state"><strong>${frame[2][0]}</strong><span>${frame[2][1]}</span></div>`;
    return `<div class="guide-window ${guide.device === "computer" ? "guide-computer" : "guide-phone"}">
      <div class="guide-window-top">BikePower <span>${guide.device === "computer" ? "模拟界面" : "系统 / App 示意"}</span></div>
      <b class="guide-window-title">${guide.title}</b>
      ${guide.labels.map((label, i) => `<div class="guide-screen-row" data-guide-target="${i}" data-active="${frame[1] === i}"><span>${label}</span><strong>${frame[2][i]}</strong></div>`).join("")}
      <div class="guide-screen-action" data-guide-target="action">${frame[3]}</div>
      <svg class="guide-pointer" viewBox="0 0 28 32" aria-hidden="true"><path d="M3 2v24l7-6 5 10 6-3-5-10h9Z" fill="var(--strong)" stroke="var(--panel)" stroke-width="2"/></svg>
      ${name === "install" ? `<div class="guide-install-progress"><i style="transform:scaleX(${position === 0 ? 0 : position === 1 ? .6 : 1})"></i></div>` : ""}
      </div>`;
  }
  function guideMarkup(name) {
    const guide = guides[name];
    if (!guide) throw new Error("未知使用演示：" + name);
    return `<div class="guide-animation" data-guide-name="${name}" data-guide-step="0" data-playing="false" data-result="ready">
      <div class="guide-animation-head"><span>模拟演示</span><span data-guide-counter>1 / ${guide.frames.length}</span></div>
      <div class="guide-animation-stage" data-guide-stage>${guideScreen(name, 0)}</div>
      <p class="guide-animation-caption" data-guide-caption role="status" aria-live="polite">${guide.frames[0][0]}</p>
      <div class="guide-animation-controls"><button type="button" class="btn primary" data-guide-play aria-pressed="false">播放演示</button><button type="button" class="btn" data-guide-next>下一步</button><button type="button" class="btn ghost" data-guide-reset>重播</button></div>
      <p class="operation-caution"><b>注意</b>${guide.note}</p></div>`;
  }
  const api = { practice, configuration, correctedPower, handoff, boardMarkup, boardSource, suiteMarkup, guides, guideMarkup };
  if (typeof module !== "undefined") module.exports = api;
  if (typeof window !== "undefined") window.BikePowerOperations = api;
  if (typeof document === "undefined") return;

  function initSuite(host) {
    host.innerHTML = suiteMarkup();
    const q = selector => host.querySelector(selector);
    const motion = matchMedia("(prefers-reduced-motion: reduce)");
    let config = configuration(null, "tick"), link = handoff(null, "tick"), exercise = practice(null, "tick", 0);
    let exerciseRequest = 0, storyRequest = 0, story = null;
    function paintConfig() {
      q("[data-config-input]").value = config.draft;
      q("[data-config-applied]").textContent = config.applied;
      q("[data-config-status]").textContent = {
        ready: "先修改左侧数值。",
        draft: "未保存，输出仍是 " + config.applied + " W。",
        error: "保存失败，草稿保留。检查连接后重试。",
        saved: "保存成功，输出已更新为 " + config.applied + " W。"
      }[config.status];
      q("[data-config-status]").dataset.result = config.status;
    }
    function paintBridge() {
      const input = q("[data-bridge-input]"), amount = q("[data-bridge-amount]"), mode = q("[data-bridge-mode]").value;
      amount.min = mode === "percent" ? -50 : -200;
      amount.max = 200;
      amount.disabled = mode === "raw";
      amount.value = Math.max(Number(amount.min), Math.min(200, Number(amount.value) || 0));
      const raw = clamp(input.value), correction = Number(amount.value), output = correctedPower(raw, mode, correction);
      q("[data-bridge-raw]").textContent = raw;
      q("[data-bridge-result]").textContent = output;
      q("[data-bridge-app]").textContent = output + " W";
      q("[data-bridge-status]").textContent = raw === 0 ? "停踩保护：0 W → 0 W。" :
        mode === "raw" ? "不校正：直接转发 " + output + " W。" :
        mode === "offset" ? raw + " W " + (correction < 0 ? "− " : "+ ") + Math.abs(correction) + " W = " + output + " W" :
        raw + " W × " + (100 + correction) + "% = " + output + " W";
    }
    function paintHandoff() {
      q(".handoff-diagram").dataset.handoffOwner = link.owner;
      q("[data-owner-web]").textContent = link.owner === "web" ? "已连接" : "已释放";
      q("[data-owner-app]").textContent = link.owner === "app" ? "已连接 · 接收数据" : "等待连接";
      q("[data-handoff-status]").textContent = link.status === "occupied" ? "连接失败：另一端仍在占用。请先释放网页连接。" :
        link.owner === "app" ? "交接成功：App 已连接，网页保持断开。" :
        link.owner === "none" ? "已释放：设备恢复广播，可以在 App 配对。" : "网页正在占用连接，先释放后再去 App。";
      q("[data-handoff-release]").disabled = link.owner !== "web";
    }
    const instructions = {
      hold: "正在按住。满 2 秒后松开，随后进入确认窗口。",
      confirm: "已松开，临时调功回滚。请在 3 秒内再按一次。",
      wifi: "二次确认成功！到手机 WiFi 设置连接 BikePower-XXXXXX。",
      ble: "蓝牙工作。可以短按调功，或按住至少 2 秒练习配网。"
    };
    let lastExerciseMode = "";
    function paintPractice(now) {
      const ready = exercise.pressed && exercise.mode === "ble" && now - exercise.start >= 2000;
      const mode = ready ? "held" : exercise.mode === "ble" && exercise.pressed ? "hold" : exercise.mode;
      const remaining = exercise.mode === "confirm" ? " · 剩余 " + Math.max(0, (exercise.deadline - now) / 1000).toFixed(1) + " s" : "";
      q("[data-practice-output]").textContent = exercise.power + " W · " +
        ({ ble: "蓝牙工作", confirm: "等待确认", wifi: "WiFi 配网" }[exercise.mode]) + remaining;
      q("[data-practice-key]").setAttribute("aria-pressed", String(exercise.pressed));
      q("[data-practice-key]").dataset.ready = String(ready);
      if (mode !== lastExerciseMode) {
        q("[data-practice-note]").textContent = mode === "held" ? "已经达到 2 秒，请松开，再在 3 秒内确认。" : instructions[mode];
        lastExerciseMode = mode;
      }
    }
    function exerciseTick(now) {
      exercise = practice(exercise, "tick", now);
      paintPractice(now);
      if (exercise.pressed || exercise.mode === "confirm") exerciseRequest = requestAnimationFrame(exerciseTick);
    }
    function exerciseEvent(event) {
      cancelAnimationFrame(exerciseRequest);
      const now = performance.now();
      exercise = practice(exercise, event, now);
      paintPractice(now);
      if (exercise.pressed || exercise.mode === "confirm") exerciseRequest = requestAnimationFrame(exerciseTick);
    }
    const key = q("[data-practice-key]");
    key.addEventListener("pointerdown", event => {
      if (event.button !== 0) return;
      key.setPointerCapture(event.pointerId);
      exerciseEvent("down");
    });
    key.addEventListener("pointerup", () => exerciseEvent("up"));
    key.addEventListener("pointercancel", () => exerciseEvent("cancel"));
    key.addEventListener("lostpointercapture", () => { if (exercise.pressed) exerciseEvent("cancel"); });
    key.addEventListener("keydown", event => {
      if (![" ", "Enter"].includes(event.key)) return;
      event.preventDefault();
      if (!event.repeat) exerciseEvent("down");
    });
    key.addEventListener("keyup", event => {
      if ([" ", "Enter"].includes(event.key)) { event.preventDefault(); exerciseEvent("up"); }
    });
    key.addEventListener("blur", () => { if (exercise.pressed) exerciseEvent("cancel"); });
    q("[data-practice-reset]").addEventListener("click", () => exerciseEvent("reset"));
    q(".practice").addEventListener("toggle", () => { if (!q(".practice").open) exerciseEvent("cancel"); });

    const stories = {
      save: [() => { config = configuration(null, "edit", { power: 260 }); paintConfig(); },
        () => { config = configuration(config, "fail"); paintConfig(); },
        () => { config = configuration(config, "save"); paintConfig(); }],
      bridge: [() => { q("[data-bridge-input]").value = 200; q("[data-bridge-mode]").value = "offset"; q("[data-bridge-amount]").value = 20; paintBridge(); },
        () => { q("[data-bridge-mode]").value = "percent"; q("[data-bridge-amount]").value = 5; paintBridge(); },
        () => { q("[data-bridge-input]").value = 0; paintBridge(); }],
      handoff: [() => { link = handoff(null, "app"); paintHandoff(); },
        () => { link = handoff(link, "release"); paintHandoff(); },
        () => { link = handoff(link, "app"); paintHandoff(); }]
    };
    const positions = { save: -1, bridge: -1, handoff: -1 };
    function stopStory() {
      cancelAnimationFrame(storyRequest);
      story = null;
      host.dataset.storyPlaying = "false";
      host.querySelectorAll("[data-story-play]").forEach(button => {
        button.textContent = motion.matches ? "逐步演示" : "播放操作";
        button.setAttribute("aria-pressed", "false");
      });
    }
    function nextStory(name) {
      positions[name] = (positions[name] + 1) % stories[name].length;
      stories[name][positions[name]]();
      host.dataset.storyStep = String(positions[name]);
      q('[data-story-state="' + name + '"]').textContent = "第 " + (positions[name] + 1) + " 步 / 3";
    }
    function storyTick(now) {
      if (!story) return;
      if (now - story.started >= 1600) {
        if (positions[story.name] === 2) { stopStory(); return; }
        nextStory(story.name);
        story.started = now;
      }
      storyRequest = requestAnimationFrame(storyTick);
    }
    host.querySelectorAll("[data-story-play]").forEach(button => {
      button.addEventListener("click", () => {
        const name = button.dataset.storyPlay;
        if (story && story.name === name) { stopStory(); return; }
        stopStory();
        if (motion.matches) { nextStory(name); return; }
        positions[name] = -1;
        nextStory(name);
        story = { name, started: performance.now() };
        host.dataset.storyPlaying = "true";
        button.textContent = "暂停";
        button.setAttribute("aria-pressed", "true");
        storyRequest = requestAnimationFrame(storyTick);
      });
    });
    host.querySelectorAll("[data-story-next]").forEach(button => button.addEventListener("click", () => { stopStory(); nextStory(button.dataset.storyNext); }));
    q("[data-config-input]").addEventListener("input", () => {
      stopStory();
      const input = q("[data-config-input]");
      if (!input.checkValidity() || input.value === "") return;
      config = configuration(config, "edit", { power: input.value }); paintConfig();
    });
    q("[data-config-save]").addEventListener("click", () => {
      stopStory();
      if (!q("[data-config-input]").reportValidity() || q("[data-config-input]").value === "") return;
      config = configuration(config, "save"); paintConfig();
    });
    q("[data-config-fail]").addEventListener("click", () => { stopStory(); config = configuration(config, "fail"); paintConfig(); });
    q("[data-config-reset]").addEventListener("click", () => { stopStory(); positions.save = -1; config = configuration(null, "tick"); paintConfig(); });
    host.querySelectorAll("[data-bridge-input],[data-bridge-mode],[data-bridge-amount]").forEach(input => input.addEventListener("input", () => {
      stopStory();
      if (input.tagName === "INPUT" && input.value === "") return;
      if (!input.checkValidity()) {
        q("[data-bridge-status]").textContent = "请输入范围内的数值：原始功率 0–2000 W，固定偏移 ±200 W，比例增加 −50% 至 +200%。";
        return;
      }
      paintBridge();
    }));
    q("[data-bridge-zero]").addEventListener("click", () => { stopStory(); q("[data-bridge-input]").value = 0; paintBridge(); });
    q("[data-bridge-reset]").addEventListener("click", () => { stopStory(); q("[data-bridge-input]").value = 200; paintBridge(); });
    q("[data-handoff-release]").addEventListener("click", () => { stopStory(); link = handoff(link, "release"); paintHandoff(); });
    q("[data-handoff-app]").addEventListener("click", () => { stopStory(); link = handoff(link, "app"); paintHandoff(); });
    q("[data-handoff-reset]").addEventListener("click", () => { stopStory(); positions.handoff = -1; link = handoff(null, "tick"); paintHandoff(); });
    host.querySelectorAll("[data-operation-tab]").forEach(button => button.addEventListener("click", () => {
      stopStory();
      exerciseEvent("cancel");
      host.dispatchEvent(new Event("demo:pause"));
      host.querySelectorAll("[data-operation-tab]").forEach(item => item.setAttribute("aria-pressed", String(item === button)));
      host.querySelectorAll("[data-operation-panel]").forEach(panel => panel.classList.toggle("hidden", panel.dataset.operationPanel !== button.dataset.operationTab));
    }));
    function pause() { stopStory(); exerciseEvent("cancel"); }
    document.addEventListener("visibilitychange", () => { if (document.hidden) pause(); });
    window.addEventListener("blur", pause);
    motion.addEventListener("change", pause);
    if ("IntersectionObserver" in window) new IntersectionObserver(entries => {
      if (!entries[0].isIntersecting) pause();
    }).observe(host);
    paintConfig(); paintBridge(); paintHandoff(); paintPractice(0); stopStory();
    if (window.BikePowerPhone) window.BikePowerPhone.mountSuite(host);
  }
  function init() {
    initManualLayout();
    document.querySelectorAll("[data-operation-demos]").forEach(initSuite);
    document.querySelectorAll("[data-operation-guide]").forEach(initGuide);
  }
  function initManualLayout() {
    const content = document.querySelector("#manual-content");
    if (!content) return;
    const chapters = ['install', 'connect', 'button', 'hotspot', 'cycling', 'running',
      'profile', 'device', 'step', 'lights', 'upstream', 'about', 'exit', 'troubleshoot', 'overview', 'compare'];
    content.querySelectorAll(".ch").forEach((old, index) => {
      const name = chapters[index], node = document.createElement("details");
      node.className = "ch manual-chapter";
      node.id = old.id;
      node.innerHTML = `<summary><h2>${name === "button" ? "按钮日常操作" : guides[name].title}</h2></summary>` +
        (name === "button" ? `<div class="operation-demos">${buttonMarkup()}</div><p class="operation-caution"><b>注意</b>配网时先松开，再确认。</p>` :
          `<div data-operation-guide="${name}"></div>`);
      old.replaceWith(node);
    });
    const hero = content.querySelector(".manual-hero");
    const operations = content.querySelector("#operations");
    const tools = document.createElement("details");
    tools.className = "manual-tools";
    tools.innerHTML = "<summary>准备动手？打开电脑工具</summary>";
    ["#manual-linkbar", "#onboarding-entry", "#tool-launchpad"].forEach(selector => {
      const node = content.querySelector(selector);
      if (node) tools.append(node);
    });
    hero.after(operations);
    operations.after(tools);
    const title = document.createElement("div");
    title.className = "manual-more";
    title.innerHTML = "<h2>其他步骤，展开看演示</h2>";
    tools.after(title);
    content.querySelectorAll('a[href^="#ch"]').forEach(link => link.addEventListener("click", () => {
      const target = document.getElementById(link.hash.slice(1));
      if (target && target.tagName === "DETAILS") target.open = true;
    }));
  }
  function initGuide(host) {
    const name = host.dataset.operationGuide, guide = guides[name];
    if (window.BikePowerPhone?.portalGuides.has(name)) {
      window.BikePowerPhone.mountGuide(host, name);
      return;
    }
    host.innerHTML = guideMarkup(name);
    const root = host.firstElementChild;
    const q = selector => root.querySelector(selector);
    const motion = matchMedia("(prefers-reduced-motion: reduce)");
    let position = 0, running = false, request = 0, started = 0;
    function paint() {
      const frame = guide.frames[position];
      root.dataset.guideStep = String(position);
      root.dataset.result = frame[4];
      root.dataset.playing = String(running);
      root.dataset.ledOn = "true";
      q("[data-guide-stage]").innerHTML = guideScreen(name, position);
      q("[data-guide-caption]").textContent = frame[0];
      q("[data-guide-counter]").textContent = (position + 1) + " / " + guide.frames.length;
      q("[data-guide-play]").textContent = motion.matches ? "逐步演示" : running ? "暂停" : position === guide.frames.length - 1 ? "重播" : "播放演示";
      q("[data-guide-play]").setAttribute("aria-pressed", String(running));
    }
    function pause() { running = false; cancelAnimationFrame(request); root.dataset.playing = "false"; q("[data-guide-play]").setAttribute("aria-pressed", "false"); q("[data-guide-play]").textContent = motion.matches ? "逐步演示" : "播放演示"; }
    function next() { position = (position + 1) % guide.frames.length; paint(); }
    function tick(now) {
      if (!running) return;
      if (name === "lights") root.dataset.ledOn = String(position === 2 || Math.floor(now / (position === 1 ? 200 : 1000)) % 2 === 0);
      if (now - started >= 1900) {
        if (position === guide.frames.length - 1) { pause(); paint(); return; }
        next(); started = now;
      }
      request = requestAnimationFrame(tick);
    }
    q("[data-guide-play]").addEventListener("click", () => {
      if (running) { pause(); return; }
      if (motion.matches) { next(); return; }
      if (position === guide.frames.length - 1) position = 0;
      running = true; started = performance.now(); paint();
      request = requestAnimationFrame(tick);
    });
    q("[data-guide-next]").addEventListener("click", () => { pause(); next(); });
    q("[data-guide-reset]").addEventListener("click", () => { pause(); position = 0; paint(); });
    document.addEventListener("visibilitychange", () => { if (document.hidden) pause(); });
    window.addEventListener("blur", pause);
    motion.addEventListener("change", pause);
    const details = host.closest("details");
    if (details) details.addEventListener("toggle", () => { if (!details.open) pause(); });
    if ("IntersectionObserver" in window) new IntersectionObserver(entries => {
      if (!entries[0].isIntersecting) pause();
    }).observe(host);
    paint();
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
  else init();
})();
