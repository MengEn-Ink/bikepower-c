"use strict";
(() => {
  const mock=window.DeviceStatusMock, api=window.DeviceStatusApi;
  const state={scenario:"normal",tab:"overview",range:60,paused:false,cpu:true,search:"",snapshot:null,latest:null,connecting:false,
    tick:0,history:[],frozenHistory:null,windowTab:"memory",windowRange:60,windowPaused:false,windowSnapshot:null,windowHistory:null};
  const $=id=>document.getElementById(id);
  const fmt=value=>value===null||value===undefined?"—":Number(value).toFixed(1);
  const text=(id,value)=>{
    const el=$(id), changed=el.textContent!==String(value);
    el.textContent=value;
    if(changed&&el.classList.contains("live-number")&&el.animate&&!window.matchMedia("(prefers-reduced-motion: reduce)").matches)
      el.animate([{opacity:.5,transform:"translateY(3px)"},{opacity:1,transform:"translateY(0)"}],{duration:220,easing:"ease-out"});
  };
  const escape=value=>String(value).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
  const hasTelemetry=()=>!["legacy","disconnected"].includes(state.scenario);
  const freePercent=s=>s.heap/mock.device.heapCapacity*100;
  const taskPercent=(task,s)=>task.name==="IDLE"?100-s.cpu:task.cpu/18.4*s.cpu;
  function percentBar(value,color="cpu"){
    return '<span class="percent-cell"><span class="percent-track '+color+'"><i style="width:'+Math.max(0,Math.min(100,value)).toFixed(2)+'%"></i></span><b>'+fmt(value)+'%</b></span>';
  }
  function composition(label,value,other,color){
    return '<div class="composition '+color+'"><div class="composition-labels"><span>'+escape(label)+" "+fmt(value)+'%</span><span>'+escape(other)+" "+fmt(100-value)+'%</span></div><div class="composition-track" role="img" aria-label="'+escape(label+" "+fmt(value)+"%，"+other+" "+fmt(100-value)+"%")+'"><i style="width:'+value.toFixed(2)+'%"></i></div></div>';
  }
  function taskTable(s,query=""){
    const tasks=mock.tasks.filter(t=>[t.name,t.status].join(" ").toLowerCase().includes(query.toLowerCase()));
    return '<table class="task-percent-table"><thead><tr><th>任务</th><th>CPU 占比 · 0–100%</th><th>最低空闲栈</th><th>状态</th></tr></thead><tbody>'+
      (tasks.length?tasks.map(t=>'<tr><td>'+escape(t.name)+'</td><td>'+(state.cpu?percentBar(taskPercent(t,s),t.name==="IDLE"?"idle":"cpu"):"未启用")+'</td><td>'+escape(t.stack===null?"—":t.stack+" B")+'</td><td>'+escape(t.status)+"</td></tr>").join(""):'<tr><td colspan="4">没有匹配的任务</td></tr>')+"</tbody></table>";
  }
  let toastTimer;
  function toast(message){text("toast",message);$("toast").hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>{$("toast").hidden=true;},3000);}
  function rows(headers,data){
    const filtered=data.filter(row=>row.join(" ").toLowerCase().includes(state.search.toLowerCase()));
    return '<table><thead><tr>'+headers.map(h=>"<th>"+escape(h)+"</th>").join("")+'</tr></thead><tbody>'+
      (filtered.length?filtered.map(row=>"<tr>"+row.map(v=>"<td>"+escape(v)+"</td>").join("")+"</tr>").join(""):'<tr><td colspan="'+headers.length+'">没有匹配的明细</td></tr>')+"</tbody></table>";
  }
  function svgPlot(metric,min,max,unit,secondary,options={}){
    const range=options.range||state.range, history=options.history||(state.paused?state.frozenHistory:state.history);
    const samples=history.slice(-range-1), count=samples.length;
    const field={memory:"heap",cpu:"cpu",rssi:"rssi",interval:"p95",largest:"largest"}[metric];
    const normalize=value=>metric==="memory"&&unit==="%"?value/mock.device.heapCapacity*100:value;
    const values=samples.map(s=>s?normalize(s[field]):null);
    const x=i=>56+i/(count-1)*648,y=v=>204-(Math.max(min,Math.min(max,v))-min)/(max-min)*164;
    const points=data=>data.map((v,i)=>x(i).toFixed(1)+","+y(v).toFixed(1)).join(" ");
    const lines=[0,1,2,3,4].map(i=>{
      const v=min+(max-min)*i/4, py=y(v);
      return '<line class="grid" x1="56" y1="'+py+'" x2="704" y2="'+py+'"/><text class="axis" x="44" y="'+(py+4)+'" text-anchor="end">'+Math.round(v)+'</text>';
    }).join("");
    const firstMissing=values.indexOf(null), cutoff=firstMissing<0?count:firstMissing;
    if(!cutoff)return '<div class="empty-chart"><b>当前时间窗没有有效样本</b><span>保留数据缺口，等待新的完整快照。</span></div>';
    const valid=values.slice(0,cutoff), pointString=points(valid);
    const endX=x(valid.length-1);
    const color=metric==="cpu"?" cpu":metric==="rssi"||metric==="interval"?" blue":"";
    const main='<polygon class="area'+color+'" points="56,204 '+pointString+' '+endX+',204"/><polyline class="series'+color+'" points="'+pointString+'"/><circle class="sample-point'+color+(state.scenario==="stale"?"":" pulsing-point")+'" cx="'+endX+'" cy="'+y(valid[valid.length-1])+'" r="3"/>';
    const second=secondary?'<polyline class="series secondary" points="'+points(samples.slice(0,cutoff).map(s=>normalize(s.largest)))+'"/>':"";
    const thresholdValue=metric==="memory"?(unit==="%"?80/mock.device.heapCapacity*100:80):80;
    const threshold=["memory","cpu"].includes(metric)?'<line class="threshold" x1="56" y1="'+y(thresholdValue)+'" x2="704" y2="'+y(thresholdValue)+'"/><text class="axis" x="704" y="'+(y(thresholdValue)-8)+'" text-anchor="end">'+(metric==="cpu"?"高负载提示 80% · 示例":"警戒线 80 KiB / "+fmt(80/mock.device.heapCapacity*100)+"%")+'</text>':"";
    return '<svg viewBox="0 0 744 248" role="img" aria-label="'+escape(unit+"趋势示例")+'"><text class="axis" x="56" y="24">'+escape(unit)+'</text>'+lines+threshold+main+second+'<text class="axis" x="56" y="232">−'+(range===60?"60 秒":"5 分钟")+'</text><text class="axis" x="380" y="232" text-anchor="middle">−'+(range===60?"30 秒":"2.5 分钟")+'</text><text class="axis" x="704" y="232" text-anchor="end">'+(state.paused&&!options.history?"暂停时刻":"现在")+'</text></svg>';
  }
  function renderChart(){
    const s=state.snapshot;
    const titles={overview:"内存空闲比例",memory:"内存比例与连续空间",cpu:"CPU 使用率",bluetooth:"板端 RSSI",events:"事件时间线"};
    text("chart-title",titles[state.tab]);
    text("chart-subtitle",state.scenario==="stale"?"遥测停更 · 保留最后有效片段":"同一采样时间轴 · 示例历史");
    $("chart-readout").textContent=!hasTelemetry()?"无遥测":state.paused?"页面已暂停":state.scenario==="stale"?"停更 "+fmt(s.age)+" 秒":"演示采样 · 1 Hz";
    $("chart-foot").innerHTML="";
    if(!hasTelemetry()){
      $("chart").innerHTML='<div class="empty-chart"><b>'+ (state.scenario==="legacy"?"已连接，固件尚未支持遥测":"连接开发板后查看运行状态")+'</b><span>'+ (state.scenario==="legacy"?"运动通知仍然可用；升级支持遥测的候选固件后，可显示内存与链路信息。":"正式功能使用 Pages 共享蓝牙连接；本原型只演示连接后的界面。")+'</span></div>';
      return;
    }
    if(state.tab==="cpu"&&!state.cpu){
      $("chart").innerHTML='<div class="empty-chart"><b>CPU 运行统计未启用</b><span>CPU 主频不能换算成使用率。启用统计并完成开销验证后，显示采样窗口内的忙碌比例与任务占用。</span></div>';
      $("chart-foot").textContent="可点上方「查看 CPU 统计规划」预览第二阶段效果。";return;
    }
    if(state.tab==="events"){
      $("chart").innerHTML='<div class="empty-chart"><b>把变化放回发生的时间</b><span>设备事件使用运行时间；网页连接事件使用本地时间。设备重启后按启动标识建立新会话。</span></div>';
      $("chart-foot").textContent="下方表格展示当前会话的事件来源与解释。";return;
    }
    let plot;
    if(state.tab==="cpu"){
      plot=composition("忙碌",s.cpu,"空闲","cpu")+svgPlot("cpu",0,100,"%",false);
      $("chart-foot").textContent="规划示例：忙碌 "+fmt(s.cpu)+"% · 空闲 "+fmt(100-s.cpu)+"% · 按采样窗口内运行计数差计算";
    }else if(state.tab==="bluetooth"){
      plot=svgPlot("rssi",-100,-30,"dBm",false);
      $("chart-foot").textContent="板端 RSSI 是信号强度；请结合通知间隔和发送失败一起判断。";
    }else{
      plot=composition("空闲",freePercent(s),"已用","memory")+svgPlot("memory",0,100,"%",true);
      $("chart-foot").innerHTML='<span class="legend"><i></i>空闲堆比例</span><span class="legend second"><i></i>最大连续块 / 总堆</span><span class="legend warn"><i></i>80 KiB = 25% · 示例容量</span>';
    }
    $("chart").innerHTML=plot;
  }
  function currentEvents(){
    const list=mock.events.map(e=>({...e}));
    if(state.scenario==="low")list.unshift({time:"01:01:04",title:"空闲堆低于项目警戒线",detail:fmt(state.snapshot.heap)+" KiB，继续观察连续空间与最低值",source:"设备",alert:true});
    if(state.scenario==="stale")list.unshift({time:"01:01:04",title:"遥测已停更 "+fmt(state.snapshot.age)+" 秒",detail:"GATT 仍连接；原因需要进一步核对",source:"网页",alert:true});
    if(state.scenario==="high")list.unshift({time:"01:01:04",title:"CPU 高负载规划演示",detail:"仅展示未来统计能力与高负载提示",source:"演示",alert:true});
    if(state.scenario==="weak")list.unshift({time:"01:01:04",title:"蓝牙信号偏弱",detail:"同时观察接收间隔与板端发送失败",source:"演示",alert:true});
    if(state.scenario==="legacy")return [{time:"00:00:02",title:"未发现遥测能力",detail:"旧固件仅提供运动数据与配置",source:"网页"}];
    if(state.scenario==="disconnected")return [{time:"00:00:03",title:"网页连接已释放",detail:"实时值已清空，开发板可供其他客户端连接",source:"网页"}];
    return list;
  }
  function renderTable(){
    const s=state.snapshot;
    const names={overview:"任务快照",memory:"内存明细",cpu:"任务 CPU 分布",bluetooth:"链路明细",events:"会话事件"};
    text("detail-title",names[state.tab]);
    $("search").placeholder=state.tab==="events"?"筛选事件内容":state.tab==="overview"||state.tab==="cpu"?"筛选任务名称":"筛选指标";
    const captions={overview:"栈余量为任务启动以来的最低空闲值 · 不是每个任务的堆内存占用",memory:"所有堆指标使用相同可分配内存口径 · 不以芯片总 SRAM 计算占用",cpu:"规划能力 · 数据为示例；任务 CPU 与 IDLE 在同一窗口内统计",bluetooth:"板端与网页分开记录 · 不把序号缺口直接命名为无线丢包",events:"启动标识区分重启 · 事件来源区分设备与网页"};
    text("detail-subtitle",captions[state.tab]);
    if(!hasTelemetry()&&state.tab!=="events"){
      $("table").innerHTML=rows(["状态","说明"],[[state.scenario==="legacy"?"遥测不可用":"未连接","无实时数据，不以 0 代替缺失"]]);return;
    }
    if(state.tab==="overview"||state.tab==="cpu"){
      $("table").innerHTML=taskTable(s,state.search);
    }else if(state.tab==="memory"){
      $("table").innerHTML=rows(["指标","数值","来源"],[
        ["内存空闲比例",fmt(freePercent(s))+"%","空闲 / 同口径总堆"],["内存已用比例",fmt(100-freePercent(s))+"%","已用 / 同口径总堆"],
        ["当前空闲堆",fmt(s.heap)+" KiB","板端"],["启动以来最低空闲堆",fmt(s.min)+" KiB","板端"],
        ["最大连续空闲块",fmt(s.largest)+" KiB","板端"],["同口径总堆容量","320.0 KiB","示例容量"],
        ["项目警戒阈值","80.0 KiB","现有配置"],["项目严重阈值","20.0 KiB","现有配置"]
      ]);
    }else if(state.tab==="bluetooth"){
      $("table").innerHTML=rows(["指标","数值","口径"],[
        ["板端 RSSI",s.rssi+" dBm","开发板接收端"],["GATT 连接间隔","30 ms","协商值 / 示例"],
        ["ATT MTU","23 B","协商值 / 示例"],["最近遥测年龄",fmt(s.age)+" s","网页前台"],
        ["通知间隔 P95",s.p95+" ms","网页接收时间"],["发送失败 / 尝试",s.failures+" / "+s.attempts,"设备应用提交"],
        ["网页缺失采样","未归因","序号缺口 ≠ 无线丢包"],["遥测角色","peripheral","不是上游功率计链路"]
      ]);
    }else $("table").innerHTML=rows(["会话时间","事件","来源","解释"],currentEvents().map(e=>[e.time,e.title,e.source,e.detail]));
  }
  function render(){
    const s=state.snapshot, available=hasTelemetry(), linked=state.scenario!=="disconnected";
    text("heap-value",available?fmt(freePercent(s)):"—");text("heap-unit",available?"%":"");
    text("heap-caption",available?"空闲 "+fmt(s.heap)+" / "+fmt(mock.device.heapCapacity)+" KiB · 启动最低 "+fmt(s.min)+" KiB":"等待固件遥测能力");
    text("memory-free-label",available?"空闲 "+fmt(freePercent(s))+"%":"空闲 —");
    text("memory-used-label",available?"已用 "+fmt(100-freePercent(s))+"%":"已用 —");
    $("heap-bar").style.width=available?(s.heap/mock.device.heapCapacity*100)+"%":"0";
    $("heap-bar").parentElement.classList.toggle("warn",state.scenario==="low");
    text("heap-chip",!available?"无数据":state.scenario==="low"?"低于警戒":state.scenario==="stale"?"历史值":"余量充足");
    $("heap-chip").classList.toggle("warn",state.scenario==="low");
    text("cpu-value",available&&state.cpu?fmt(s.cpu):"—");text("cpu-unit",available&&state.cpu?"%":"");
    text("cpu-caption",available&&state.cpu?"主频 160 MHz · CPU 示例采样":"未开启运行统计");
    text("cpu-busy-label",available&&state.cpu?"忙碌 "+fmt(s.cpu)+"%":"忙碌 —");
    text("cpu-idle-label",available&&state.cpu?"空闲 "+fmt(100-s.cpu)+"%":"空闲 —");
    $("cpu-bar").style.width=available&&state.cpu?s.cpu+"%":"0";
    $("cpu-bar").parentElement.classList.toggle("warn",available&&state.cpu&&s.cpu>=80);
    text("cpu-demo",state.cpu?"模拟 CPU 统计未启用 →":"开启 CPU 百分比演示 →");
    $("cpu-demo").disabled=!available;
    text("rssi-value",s.rssi===null?"—":s.rssi);text("rssi-unit",available?"dBm":"");
    text("ble-caption",available?"最近通知 "+fmt(s.age)+" s · 接收间隔 P95 "+s.p95+" ms":"板端链路数据不可用");
    text("ble-chip",!available?"无遥测":state.scenario==="stale"?"数据停更":state.scenario==="weak"?"信号偏弱":"持续收样");
    $("ble-chip").classList.toggle("warn",["stale","weak"].includes(state.scenario));
    text("connection-state",linked?"已连接 · 演示":"未连接 · 演示");
    $("connection-dot").classList.toggle("off",!linked);
    text("connection-action",state.connecting?"正在连接演示…":linked?"释放连接":"连接演示设备");
    $("connection-action").disabled=state.connecting;
    $("release-bottom").disabled=!linked;
    $("pause").disabled=!available;text("pause",state.paused?"继续页面":"暂停页面");
    $("scenario").value=state.scenario;
    const notices={
      normal:"<b>持续收样。</b> 内存与 CPU 用百分比观察余量，蓝牙保留 dBm 和间隔；当前均为离线演示数据。",
      low:"<b>内存低于警戒线。</b> 当前空闲堆 "+fmt(s.heap)+" KiB，低于项目现有 80 KiB 阈值；观察是否继续下降。",
      stale:"<b>遥测中断，已冻结最后值。</b> "+fmt(s.age)+" 秒未更新，不能仅凭网页接收间隔判断无线丢包。",
      high:"<b>CPU 高负载规划演示。</b> 当前示例 "+fmt(s.cpu)+"%，展示高负载颜色与动画；实际固件统计尚未接入。",
      weak:"<b>蓝牙信号偏弱。</b> 示例 RSSI "+s.rssi+" dBm；信号强弱不能直接证明无线丢包。",
      legacy:"<b>蓝牙已连接，固件尚未支持遥测。</b> 运动数据仍可在实时监控查看；本页不显示虚构的运行指标。",
      disconnected:"<b>网页连接已释放。</b> 实时值已清空。正式功能中，开发板可重新广播供骑行 App 或码表连接。"
    };
    $("notice").innerHTML=state.paused&&available?"<b>页面已暂停。</b> 演示采样与独立窗口继续更新；继续页面后回到最新快照。":notices[state.scenario];
    $("notice").className="notice"+(["low","high"].includes(state.scenario)?" error":["stale","legacy","weak"].includes(state.scenario)||state.paused?" warn":"");
    text("firmware",state.scenario==="legacy"?"v4.2.1 · 示例旧版":"遥测候选 · 示例");
    text("uptime",available?duration(s.uptime):"—");
    text("capability",available?"内存 / CPU 演示 / 链路":state.scenario==="legacy"?"未提供遥测":"等待连接");
    text("sampling-badge",available?"1 Hz":"—");
    text("sequence",s.sequence===null?"—":"#"+s.sequence);
    text("interval",available?"30 ms":"—");text("mtu",available?"23 B":"—");
    text("tx",available?s.failures+" / "+s.attempts:"—");text("disconnects",linked?"0 次":"1 次 · 示例");
    $("event-list").innerHTML=currentEvents().slice(0,3).map(e=>'<li class="'+(e.alert?"alert":"")+'"><time>'+escape(e.time)+'</time><b>'+escape(e.title)+'</b>'+escape(e.detail)+"</li>").join("");
    text("sample-state",!available?"无有效遥测":state.paused?"已暂停画面":state.scenario==="stale"?"历史快照 · 未更新":"示例快照 · 不是实测");
    text("sample-count",available?"窗口 "+state.range+" s · 示例":"等待遥测");
    document.querySelectorAll("[data-tab]").forEach(b=>{const active=b.dataset.tab===state.tab;b.classList.toggle("selected",active);b.setAttribute("aria-pressed",String(active));});
    document.querySelectorAll("[data-range]").forEach(b=>{const active=Number(b.dataset.range)===state.range;b.classList.toggle("selected",active);b.setAttribute("aria-pressed",String(active));b.disabled=!available;});
    renderChart();renderTable();renderResident();renderWindow();
  }
  async function changeScenario(value){
    await api.readDemoSnapshot(value);
    state.scenario=value;state.paused=false;state.windowPaused=false;state.tick=0;
    if(value==="high")state.cpu=true;
    state.history=Array.from({length:301},(_,i)=>{
      if(value==="stale"&&i>291)return null;
      return mock.sample(value,i-300);
    });
    state.latest=mock.sample(value,0);state.snapshot={...state.latest};
    state.frozenHistory=null;state.windowSnapshot=null;state.windowHistory=null;render();
  }
  function changeTab(tab){
    state.tab=tab;state.search="";$("search").value="";location.hash=tab;render();
  }
  function readHash(){
    const tab=location.hash.slice(1);if(["overview","memory","cpu","bluetooth","events"].includes(tab)){state.tab=tab;state.search="";$("search").value="";if(state.snapshot)render();}
  }
  $("scenario").addEventListener("change",e=>changeScenario(e.target.value));
  document.querySelectorAll("[data-tab]").forEach(b=>b.addEventListener("click",()=>changeTab(b.dataset.tab)));
  document.querySelectorAll("[data-range]").forEach(b=>b.addEventListener("click",()=>{state.range=Number(b.dataset.range);render();}));
  $("search").addEventListener("input",e=>{state.search=e.target.value;renderTable();});
  $("all-events").addEventListener("click",()=>changeTab("events"));
  $("pause").addEventListener("click",()=>{
    state.paused=!state.paused;
    if(state.paused)state.frozenHistory=state.history.slice();
    else{state.snapshot={...state.latest};state.frozenHistory=null;}
    render();
  });
  $("cpu-demo").addEventListener("click",()=>{state.cpu=!state.cpu;if(state.cpu)changeTab("cpu");else render();});
  async function connectionAction(){
    if(state.connecting)return;
    if(state.scenario!=="disconnected"){await changeScenario("disconnected");toast("已释放演示连接；本原型没有操作真实蓝牙。");return;}
    state.connecting=true;render();
    try{await api.connectDemo();await changeScenario("normal");toast("演示设备已连接。全部数值仍为示例。");}
    finally{state.connecting=false;render();}
  }
  $("connection-action").addEventListener("click",connectionAction);
  $("release-bottom").addEventListener("click",connectionAction);
  $("theme").addEventListener("click",()=>{
    const dark=document.documentElement.dataset.theme!=="dark";document.documentElement.dataset.theme=dark?"dark":"light";text("theme",dark?"浅色外观":"深色外观");
  });
  $("export").addEventListener("click",()=>{
    const report={isDemo:true,notice:"设计原型示例，不能用于设备验收",exportedAt:new Date().toISOString(),scenario:state.scenario,cpuPlanningDemo:state.cpu,telemetry:state.latest,pagePaused:state.paused,windowPaused:state.windowPaused,events:currentEvents()};
    const url=URL.createObjectURL(new Blob([JSON.stringify(report,null,2)],{type:"application/json"}));
    const a=document.createElement("a");a.href=url;a.download="bikepower-device-status-DEMO.json";document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);
    toast("已导出演示报告，内含示例数据声明。");
  });
  function duration(seconds){
    return [Math.floor(seconds/3600),Math.floor(seconds/60)%60,seconds%60].map(v=>String(v).padStart(2,"0")).join(":");
  }
  function sparkline(field,min,max,color){
    const history=(state.paused?state.frozenHistory:state.history).slice(-31);
    const points=history.map((s,i)=>s?i*8+","+ (44-(s[field]-min)/(max-min)*36).toFixed(1):null).filter(Boolean);
    return '<svg viewBox="0 0 240 52"><polyline class="spark-series '+color+'" points="'+points.join(" ")+'"/></svg>';
  }
  function renderResident(){
    const s=state.snapshot,available=hasTelemetry(),live=available&&!state.paused&&state.scenario!=="stale";
    text("resident-heap",available?fmt(freePercent(s))+"% 空闲":"—");
    text("resident-cpu",!available?"—":state.cpu?fmt(s.cpu)+"%":"未启用");
    text("resident-rssi",available?s.rssi+" dBm":"—");
    text("resident-state",!available?"无遥测 · 演示":state.paused?"页面暂停 · 窗口独立更新":state.scenario==="stale"?"数据停更 · 历史值":"离线演示 · 每秒更新");
    $("resident-dot").classList.toggle("off",!live);
    document.body.dataset.live=String(live);
    ["memory","cpu","bluetooth"].forEach((type,i)=>{
      const alert=available&&((i===0&&s.heap<80)||(i===1&&state.cpu&&s.cpu>=80)||(i===2&&s.rssi<=-80));
      $(type+"-card").classList.toggle("metric-alert",alert);
      document.querySelector('.resident-item[data-open-window="'+type+'"]').classList.toggle("metric-alert",alert);
    });
    $("memory-spark").innerHTML=available?sparkline("heap",0,mock.device.heapCapacity,"memory"):"";
    $("cpu-spark").innerHTML=available&&state.cpu?sparkline("cpu",0,100,"cpu"):"";
    $("bluetooth-spark").innerHTML=available?sparkline("rssi",-100,-30,"bluetooth"):"";
  }
  function renderWindow(){
    const dialog=$("monitor-window");if(!dialog.open)return;
    const s=state.windowPaused?state.windowSnapshot:state.latest;
    const history=state.windowPaused?state.windowHistory:state.history;
    const type=state.windowTab,available=hasTelemetry(),active=available&&state.scenario!=="stale"&&!state.windowPaused;
    dialog.dataset.metric=type;dialog.dataset.live=String(active);
    dialog.classList.toggle("window-alert",available&&((type==="memory"&&s.heap<80)||(type==="cpu"&&state.cpu&&s.cpu>=80)||(type==="bluetooth"&&s.rssi<=-80)));
    text("window-title",{memory:"内存活动监控",cpu:"CPU 活动监控",bluetooth:"蓝牙链路监控"}[type]);
    text("window-live-state",!available?"无遥测":state.windowPaused?"窗口暂停":state.scenario==="stale"?"遥测停更":"● 实时演示");
    $("window-pause").disabled=!available;text("window-pause",state.windowPaused?"继续窗口":"暂停窗口");
    text("window-sample",available?"快照 #"+s.sequence+" · 运行 "+duration(s.uptime):"等待有效遥测");
    document.querySelectorAll("[data-window-tab]").forEach(b=>{const selected=b.dataset.windowTab===type;b.classList.toggle("selected",selected);b.setAttribute("aria-pressed",String(selected));});
    document.querySelectorAll("[data-window-range]").forEach(b=>{const selected=Number(b.dataset.windowRange)===state.windowRange;b.classList.toggle("selected",selected);b.setAttribute("aria-pressed",String(selected));b.disabled=!available;});
    let metrics=[],data=[],plot="";
    const options={range:state.windowRange,history};
    if(!available){
      metrics=[["状态","无遥测"],["能力",state.scenario==="legacy"?"旧固件":"未连接"]];
      plot='<div class="empty-chart"><b>无有效遥测数据</b><span>窗口保留，等待重新连接或支持遥测的固件。</span></div>';
      data=[["当前状态","—","不将缺失值显示为 0"]];
    }else if(type==="memory"){
      metrics=[["空闲比例",fmt(freePercent(s))+"%"],["已用比例",fmt(100-freePercent(s))+"%"],["最大连续块",fmt(s.largest)+" KiB"]];
      plot=composition("空闲",freePercent(s),"已用","memory")+svgPlot("memory",0,100,"%",true,options);
      data=[["当前空闲堆",fmt(s.heap)+" KiB",fmt(freePercent(s))+"% 总堆"],["启动最低空闲",fmt(s.min)+" KiB",fmt(s.min/mock.device.heapCapacity*100)+"% 总堆"],["可分配容量",fmt(mock.device.heapCapacity)+" KiB","同口径 / 示例"],["警戒阈值","80.0 KiB","25% / 示例容量"],["严重阈值","20.0 KiB","6.25% / 示例容量"]];
    }else if(type==="cpu"){
      metrics=[["CPU 忙碌",state.cpu?fmt(s.cpu)+"%":"未启用"],["CPU 空闲",state.cpu?fmt(100-s.cpu)+"%":"—"],["配置主频","160 MHz"]];
      plot=state.cpu?composition("忙碌",s.cpu,"空闲","cpu")+svgPlot("cpu",0,100,"%",false,options):'<div class="empty-chart"><b>CPU 统计规划</b><span>开启页面上的 CPU 规划演示后，窗口同步显示变化；不代表固件已支持。</span></div>';
      data=mock.tasks.map(t=>[t.name,state.cpu?fmt(taskPercent(t,s))+"%":"未启用",t.stack===null?"—":t.stack+" B 最低空闲栈"]);
    }else{
      metrics=[["板端 RSSI",s.rssi+" dBm"],["接收间隔 P95",s.p95+" ms"],["发送失败",s.failures+" / "+s.attempts]];
      plot=svgPlot("rssi",-100,-30,"dBm",false,options)+svgPlot("interval",800,2800,"ms",false,options);
      data=[["最近遥测年龄",fmt(s.age)+" s","网页前台"],["协商连接间隔","30 ms","示例"],["ATT MTU","23 B","示例"],["链路角色","peripheral","开发板 ↔ 网页"]];
    }
    $("window-metrics").innerHTML=metrics.map(m=>'<article><span>'+escape(m[0])+'</span><b>'+escape(m[1])+'</b></article>').join("");
    text("window-chart-title",type==="bluetooth"?"信号强度与接收间隔":type==="memory"?"内存百分比 · 同口径总堆":"CPU 百分比 · 规划示例");
    $("window-chart").innerHTML=plot;
    // 窗口明细独立于页面搜索，不借用页面筛选状态。
    $("window-table").innerHTML=available&&type==="cpu"?taskTable(s):'<table><thead><tr><th>指标 / 任务</th><th>数值</th><th>口径</th></tr></thead><tbody>'+data.map(r=>"<tr>"+r.map(v=>"<td>"+escape(v)+"</td>").join("")+"</tr>").join("")+"</tbody></table>";
  }
  let windowOpener=null;
  function openWindow(type){
    const dialog=$("monitor-window");windowOpener=document.activeElement;
    if(type)state.windowTab=type;
    dialog.classList.remove("collapsed");
    text("window-minimize","折叠");
    if(!dialog.open)dialog.show();
    renderWindow();
  }
  document.querySelectorAll("[data-open-window]").forEach(b=>b.addEventListener("click",()=>openWindow(b.dataset.openWindow)));
  $("open-window").addEventListener("click",()=>openWindow(["memory","cpu","bluetooth"].includes(state.tab)?state.tab:"memory"));
  $("window-close").addEventListener("click",()=>{$("monitor-window").close();});
  $("monitor-window").addEventListener("close",()=>{if(windowOpener)windowOpener.focus();});
  document.addEventListener("keydown",e=>{if(e.key==="Escape"&&$("monitor-window").open){e.preventDefault();$("monitor-window").close();}});
  $("window-minimize").addEventListener("click",()=>{
    const collapsed=$("monitor-window").classList.toggle("collapsed");text("window-minimize",collapsed?"展开":"折叠");
  });
  $("window-maximize").addEventListener("click",()=>{
    const dialog=$("monitor-window"),maximized=dialog.classList.toggle("maximized");
    dialog.classList.remove("collapsed");text("window-minimize","折叠");
    ["left","top","width","height"].forEach(p=>dialog.style.removeProperty(p));
    text("window-maximize",maximized?"还原":"放大");
  });
  document.querySelectorAll("[data-window-tab]").forEach(b=>b.addEventListener("click",()=>{state.windowTab=b.dataset.windowTab;renderWindow();}));
  document.querySelectorAll("[data-window-range]").forEach(b=>b.addEventListener("click",()=>{state.windowRange=Number(b.dataset.windowRange);renderWindow();}));
  $("window-pause").addEventListener("click",()=>{
    state.windowPaused=!state.windowPaused;
    if(state.windowPaused){state.windowSnapshot={...state.latest};state.windowHistory=state.history.slice();}
    else{state.windowSnapshot=null;state.windowHistory=null;}
    renderWindow();
  });
  let drag=null;
  const handle=$("window-drag-handle"),dialog=$("monitor-window");
  handle.addEventListener("pointerdown",e=>{
    if(e.target.closest("button")||e.button!==0||dialog.classList.contains("maximized")||window.innerWidth<=600)return;
    const r=dialog.getBoundingClientRect();drag={dx:e.clientX-r.left,dy:e.clientY-r.top};
    handle.setPointerCapture(e.pointerId);
  });
  handle.addEventListener("pointermove",e=>{
    if(!drag)return;
    dialog.style.left=Math.max(8,Math.min(window.innerWidth-dialog.offsetWidth-8,e.clientX-drag.dx))+"px";
    dialog.style.top=Math.max(8,Math.min(window.innerHeight-72,e.clientY-drag.dy))+"px";
  });
  handle.addEventListener("pointerup",()=>{drag=null;});
  handle.addEventListener("pointercancel",()=>{drag=null;});
  handle.addEventListener("lostpointercapture",()=>{drag=null;});
  window.addEventListener("resize",()=>{
    if(!dialog.open)return;
    const r=dialog.getBoundingClientRect();
    if(r.right>window.innerWidth||r.top>window.innerHeight-72){dialog.style.removeProperty("left");dialog.style.removeProperty("top");}
  });
  setInterval(()=>{
    if(!state.latest||!hasTelemetry()||document.hidden)return;
    state.tick++;
    const previous=state.latest;
    state.latest=mock.sample(state.scenario,state.tick);
    state.latest.min=Math.min(previous.min,state.latest.min,state.latest.heap);
    state.history.push(state.scenario==="stale"?null:{...state.latest});
    if(state.history.length>301)state.history.shift();
    if(!state.paused)state.snapshot={...state.latest};
    render();
  },1000);
  window.addEventListener("hashchange",readHash);readHash();changeScenario("normal");
})();
