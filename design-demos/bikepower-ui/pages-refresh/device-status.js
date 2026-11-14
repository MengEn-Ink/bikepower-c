/* 页面常驻栏和独立窗口共享一份真实遥测订阅。 */
(function(){
  "use strict";
  const $=id=>document.getElementById(id),api=window.BikePowerTelemetry;
  const host=$("mode-device-status");
  if(!host||!api)return;
  let rec=null,attachEpoch=0,pageFrozen=null,windowFrozen=null,lastVisible=Date.now();
  let pageKey="memory",windowKey="memory",pageRange=60000,windowRange=60000;
  const esc=value=>String(value??"—").replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
  const pct=value=>value===null||value===undefined?"—":value.toFixed(1)+"%";
  const kib=value=>value===undefined?"—":(value/1024).toFixed(1)+" KiB";
  const metric=(key,title)=>`<button class="telemetry-metric" data-telemetry-open="${key}" type="button" title="查看${title}详情"><span>${title}</span><strong id="telemetry-${key}-value">—</strong><small id="telemetry-${key}-sub">—</small></button>`;
  const bar=document.createElement("div");bar.className="telemetry-bar";bar.id="telemetry-bar";
  bar.setAttribute("aria-label","设备实时状态");
  bar.innerHTML=metric("memory","堆")+metric("cpu","CPU")+metric("link","信号");
  $("theme-btn").before(bar);
  const select=(id)=>`<label>指标 <select id="${id}"><option value="memory">内存</option><option value="cpu">CPU</option><option value="link">蓝牙</option></select></label>`;
  const range=(id)=>`<label>时间 <select id="${id}"><option value="60000">最近 1 分钟</option><option value="300000">最近 5 分钟</option></select></label>`;
  host.classList.add("telemetry-page");
  host.innerHTML=`<div class="telemetry-heading"><h2>设备状态</h2><div class="actions"><button class="btn primary" id="telemetry-connect">连接设备</button><button class="btn ghost" id="telemetry-release">释放网页连接</button><button class="btn ghost" id="telemetry-theme">切换主题</button></div></div>
    <p>观察开发板的内存余量、CPU 百分比与蓝牙链路。与本站其他工具共用连接；连接骑行 App 或码表前，请释放网页连接。</p>
    <div id="telemetry-state" class="telemetry-state" role="status">未连接 · 点击连接设备</div>
    <div class="telemetry-controls">${select("telemetry-page-kind")}${range("telemetry-page-range")}<button class="btn ghost sm" id="telemetry-page-pause">暂停画面</button><button class="btn ghost sm" id="telemetry-export">导出真实采样</button></div>
    <div id="telemetry-page-chart"></div><div class="telemetry-grid"><div id="telemetry-page-detail" class="telemetry-detail"></div><div id="telemetry-session" class="telemetry-detail"></div></div>
    <p class="telemetry-note">内存比例采用同口径可分配堆容量。CPU 按一秒窗口计算，主频显示配置值。RSSI 是板端接收信号；提交成功计数不代表对端已接收，网页间隔不代表无线丢包。</p>`;
  const dialog=document.createElement("section");dialog.id="telemetry-window";dialog.className="telemetry-window hidden";
  dialog.setAttribute("role","dialog");dialog.setAttribute("aria-modal","false");dialog.setAttribute("aria-labelledby","telemetry-window-title");
  dialog.innerHTML=`<div class="telemetry-window-head"><h3 id="telemetry-window-title">状态详情</h3><button class="btn ghost sm" id="telemetry-window-collapse" aria-expanded="true">折叠</button><button class="btn ghost sm" id="telemetry-window-max">放大</button><button class="btn ghost sm" id="telemetry-window-close">关闭</button></div>
    <div class="telemetry-window-body"><div class="telemetry-controls">${select("telemetry-window-kind")}${range("telemetry-window-range")}<button class="btn ghost sm" id="telemetry-window-pause">暂停窗口</button></div><div id="telemetry-window-state" class="telemetry-state"></div><div id="telemetry-window-chart"></div><div id="telemetry-window-detail" class="telemetry-detail"></div></div>`;
  document.querySelector(".wrap").append(dialog);
  const session=new api.Session(()=>render());
  function snapshot(frozen){
    const history=frozen||session.model.history;
    const active=["live","waiting"].includes(session.state);
    return {history,sample:active?history.at(-1):null};
  }
  function status(){
    if(document.hidden)return "页面在后台 · 暂停停更判断";
    if(session.state==="unsupported")return "已连接，但固件没有遥测服务 · 请刷入含设备遥测的候选固件";
    if(session.state==="error")return "读取失败："+session.error+" · 点击连接设备重新授权并重试";
    if(session.state==="loading")return "正在订阅设备遥测…";
    if(session.state==="disconnected")return "未连接 · "+(navigator.bluetooth?"点击连接设备":"此浏览器不支持 Web Bluetooth；请用支持蓝牙的 Chrome / Edge");
    const latest=session.model.history.at(-1);
    if(!latest)return "等待完整的设备快照…";
    const age=Date.now()-latest.at;
    const stale=Date.now()-lastVisible>3*session.model.meta.period&&age>3*session.model.meta.period;
    return `${rec?.device?.name||"设备"} · ${stale?"遥测停更":"持续采样"} · 最近快照 ${(age/1000).toFixed(1)} 秒前${session.error?" · "+session.error:""}`;
  }
  function rows(items){return items.map(([a,b])=>`<div class="telemetry-row"><span>${esc(a)}</span><b>${esc(b)}</b></div>`).join("");}
  function detail(key,s){
    if(!s)return "<h3>等待真实设备数据</h3><p>连接后显示测量值；旧固件与缺失能力显示“—”。</p>";
    if(key==="memory")return "<h3>内存余量</h3>"+rows([["空闲 / 已用",`${pct(s.freePercent)} / ${pct(100-s.freePercent)}`],["当前空闲",kib(s.free)],["启动最低空闲",kib(s.minimum)],["最大连续块",kib(s.largest)],["总可分配堆",kib(s.total)],["警戒 / 严重线","80 KiB / 20 KiB"]]);
    if(key==="cpu"){
      const busy=s.cpu,main=s.mainCpu,other=busy===null?null:Math.max(0,busy-main);
      return "<h3>CPU 窗口占比</h3>"+[["主任务",main],["其余任务合计",other],["IDLE 空闲",busy===null?null:100-busy]].map(([name,value])=>`<div class="telemetry-cpu-row">${name}<span>${pct(value)}</span><div class="telemetry-track"><i style="width:${value??0}%"></i></div></div>`).join("")+rows([["配置主频",s.mhz+" MHz"],["主任务最低空闲栈",kib(s.mainStack)]])+"<p class='telemetry-note'>其余任务只报告合计。没有扫描全任务表，也没有把主循环耗时当作全机 CPU。</p>";
    }
    return "<h3>板与网页的连接</h3>"+rows([["板端 RSSI",s.rssi===null?"—":s.rssi+" dBm"],["ATT MTU",s.mtu||"—"],["连接间隔",s.interval+" ms"],["Peripheral latency",s.latency],["监督超时",s.timeout+" ms"],["提交成功",s.submitted],["提交失败",s.failed],["mbuf 分配失败",s.allocationFailed],["通知锁跳过",s.lockSkipped]]);
  }
  function chart(key,history,rangeMs,width){
    const W=Math.max(300,Math.min(900,width)),H=230,left=48,right=12,top=18,bottom=30;
    const end=history.at(-1)?.at||Date.now(),start=end-rangeMs;
    const filtered=history.filter(s=>s.at>=start);
    const min=key==="link"?-100:0,max=key==="link"?-30:100;
    const names=key==="memory"?["freePercent","largestPercent"]:key==="cpu"?["cpu"]:["rssi"];
    const colors=key==="memory"?["var(--tm-memory)","var(--tm-link)"]:key==="cpu"?["var(--tm-cpu)"]:["var(--tm-link)"];
    const x=t=>left+(t-start)/rangeMs*(W-left-right),y=v=>top+(max-Math.max(min,Math.min(max,v)))/(max-min)*(H-top-bottom);
    let svg=`<svg class="telemetry-chart" viewBox="0 0 ${W} ${H}" role="img" aria-label="${key==="link"?"RSSI dBm":"百分比 0 到 100"}真实时间趋势">`;
    for(let i=0;i<=4;i++){const value=min+(max-min)*i/4;svg+=`<line x1="${left}" x2="${W-right}" y1="${y(value)}" y2="${y(value)}" stroke="var(--line-soft)"/><text x="1" y="${y(value)+4}" fill="var(--muted)" font-size="12">${Math.round(value)}${key==="link"?"":"%"}</text>`;}
    names.forEach((name,index)=>{
      let d="",previous=null;
      for(const s of filtered){
        const value=s[name];if(value===null||value===undefined){previous=null;continue;}
        const gap=!previous||s.boot!==previous.boot||s.at-previous.at>3*(session.model.meta?.period||1000);
        d+=(gap?"M":"L")+x(s.at).toFixed(1)+","+y(value).toFixed(1);previous=s;
      }
      svg+=`<path d="${d}" fill="none" stroke="${colors[index]}" stroke-width="2.5" stroke-linejoin="round"/>`;
      const last=filtered.at(-1);if(last?.[name]!=null)svg+=`<circle cx="${x(last.at)}" cy="${y(last[name])}" r="3.5" fill="${colors[index]}"/>`;
    });
    svg+=`<text x="${left}" y="${H-7}" fill="var(--muted)" font-size="12">−${rangeMs/60000} 分钟</text><text x="${W-54}" y="${H-7}" fill="var(--muted)" font-size="12">最新</text></svg>`;
    return svg+`<p class="telemetry-note">${key==="memory"?"绿色：空闲堆占比 · 蓝色：最大连续块占比":key==="cpu"?"琥珀色：CPU 忙碌百分比":"蓝色：板端 RSSI（dBm）"} · 断层不连接</p>`;
  }
  function render(){
    if(!session)return;
    const p=snapshot(pageFrozen),s=p.sample;
    [["memory",s?kib(s.free):"—",s?pct(s.freePercent)+" 空闲":"—"],
      ["cpu",s?pct(s.cpu):"—",s&&s.cpu!==null?pct(100-s.cpu)+" 空闲":"—"],
      ["link",s?.rssi!=null?s.rssi+" dBm":"—",s?s.interval+" ms":"—"]].forEach(([key,value,sub])=>{
        const label=$("telemetry-"+key+"-value");
        if(label.textContent!==value&&label.animate&&!matchMedia("(prefers-reduced-motion: reduce)").matches){
          label.animate([{transform:"translateY(-2px)",opacity:.75},{transform:"none",opacity:1}],{duration:350,easing:"ease-out"});
        }
        label.textContent=value;$("telemetry-"+key+"-sub").textContent=sub;
      });
    bar.querySelector('[data-telemetry-open="memory"]').dataset.warning=String(!!s&&s.free<81920);
    $("telemetry-state").textContent=status()+(pageFrozen?" · 画面已暂停":"");
    $("telemetry-page-chart").innerHTML=chart(pageKey,p.history,pageRange,host.clientWidth||900);
    $("telemetry-page-detail").innerHTML=detail(pageKey,s);
    const meta=session.model.meta;
    $("telemetry-session").innerHTML="<h3>设备与会话</h3>"+rows(meta?[["真实构建版本",meta.firmware],["板型",meta.board],["源码版本",meta.git],["设备启动会话",meta.boot],["重启原因（ESP-IDF）",meta.reset],["运行时间",s?s.uptime+" s":"—"],["快照序号",s?.sequence??"—"],["历史样本",p.history.length]]:[["遥测能力","等待连接"]]);
    if(!dialog.classList.contains("hidden")){
      const w=snapshot(windowFrozen);
      $("telemetry-window-state").textContent=status()+(windowFrozen?" · 窗口已暂停":"");
      $("telemetry-window-chart").innerHTML=chart(windowKey,w.history,windowRange,dialog.clientWidth-40);
      $("telemetry-window-detail").innerHTML=detail(windowKey,w.sample);
    }
  }
  async function attach(record){
    const epoch=++attachEpoch;rec=record;pageFrozen=windowFrozen=null;
    $("telemetry-page-pause").textContent="暂停画面";$("telemetry-window-pause").textContent="暂停窗口";
    try{await session.start(record.server);}catch(e){if(epoch===attachEpoch)render();}
  }
  function detach(){
    ++attachEpoch;rec=null;pageFrozen=windowFrozen=null;
    session.stop();render();
  }
  function mode(){
    document.body.classList.toggle("device-status-mobile",currentMode==="device-status");
    render();
  }
  bar.addEventListener("click",e=>{
    const button=e.target.closest("[data-telemetry-open]");if(!button)return;
    windowKey=button.dataset.telemetryOpen;$("telemetry-window-kind").value=windowKey;
    dialog.classList.remove("hidden");render();$("telemetry-window-close").focus();
  });
  $("telemetry-connect").onclick=()=>{if(compatibilityHandoffActive())setCompatibilityHandoff(false);connectAnyDevice();};
  $("telemetry-release").onclick=()=>{setCompatibilityHandoff(true);detach();};
  $("telemetry-theme").onclick=()=>toggleTheme();
  $("telemetry-page-kind").onchange=e=>{pageKey=e.target.value;render();};
  $("telemetry-window-kind").onchange=e=>{windowKey=e.target.value;render();};
  $("telemetry-page-range").onchange=e=>{pageRange=Number(e.target.value);render();};
  $("telemetry-window-range").onchange=e=>{windowRange=Number(e.target.value);render();};
  $("telemetry-page-pause").onclick=()=>{pageFrozen=pageFrozen?null:session.model.history.slice();$("telemetry-page-pause").textContent=pageFrozen?"继续画面":"暂停画面";render();};
  $("telemetry-window-pause").onclick=()=>{windowFrozen=windowFrozen?null:session.model.history.slice();$("telemetry-window-pause").textContent=windowFrozen?"继续窗口":"暂停窗口";render();};
  function closeWindow(){dialog.classList.add("hidden");bar.querySelector(`[data-telemetry-open="${windowKey}"]`).focus();}
  $("telemetry-window-close").onclick=closeWindow;
  dialog.addEventListener("keydown",e=>{if(e.key==="Escape")closeWindow();});
  $("telemetry-window-collapse").onclick=e=>{const collapsed=dialog.classList.toggle("collapsed");e.target.textContent=collapsed?"展开":"折叠";e.target.setAttribute("aria-expanded",String(!collapsed));};
  $("telemetry-window-max").onclick=e=>{e.target.textContent=dialog.classList.toggle("maximized")?"还原":"放大";};
  const heading=dialog.querySelector(".telemetry-window-head");
  heading.onpointerdown=e=>{
    if(e.target.closest("button")||dialog.classList.contains("maximized"))return;
    const rect=dialog.getBoundingClientRect(),dx=e.clientX-rect.left,dy=e.clientY-rect.top;
    heading.setPointerCapture(e.pointerId);
    heading.onpointermove=event=>{dialog.style.left=Math.max(0,Math.min(innerWidth-rect.width,event.clientX-dx))+"px";dialog.style.top=Math.max(0,Math.min(innerHeight-60,event.clientY-dy))+"px";};
    heading.onpointerup=heading.onpointercancel=()=>{heading.onpointermove=null;};
  };
  $("telemetry-export").onclick=()=>{
    const blob=new Blob([JSON.stringify({source:"device-ble-telemetry",exportedAt:new Date().toISOString(),meta:session.model.meta,connected:session.state==="live",samples:session.model.history},null,2)],{type:"application/json"});
    const url=URL.createObjectURL(blob),a=document.createElement("a");a.href=url;a.download="bikepower-device-telemetry.json";a.click();URL.revokeObjectURL(url);
  };
  document.addEventListener("visibilitychange",()=>{
    if(!document.hidden){lastVisible=Date.now();session.model.resetWindow();render();}
  });
  window.BikePowerStatus={attach,detach,mode};
  mode();
  if(typeof singleDevice!=="undefined"&&singleDevice?.server?.connected)attach(singleDevice);
  setInterval(()=>{if(!document.hidden)render();},1000);
})();
