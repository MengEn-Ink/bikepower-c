/* 骑行适配排查：独立两侧快照，逐台采集优先，不向设备写入。 */
(function(){
  "use strict";
  const model=window.BikePowerCompatibility;
  const KEY="bikepower.compatibility.capture.v1";
  const outcomes=["discovery","connection","data","reconnect","control"];
  const contextIds=["target","version","platform","role","stage","reference-works"];
  const el=id=>document.getElementById("compat-"+id);
  const escape=value=>escapeHtml(String(value??""));
  let board=null,reference=null,test=null,busy=false,generation=0;
  const live=new Map(),owners=new WeakMap();
  const SAMPLE_MS=10000,MAX_READS=96,MAX_SUBSCRIPTIONS=24;
  function context(){return model.validateContext({target:el("target").value,version:el("version").value,platform:el("platform").value,role:el("role").value,stage:el("stage").value,referenceWorks:el("reference-works").checked});}
  function report(){return model.makeReport(context(),board,reference,test);}
  function status(text,kind="warn"){el("status").textContent=text;el("status").className="badge "+kind;}
  function clearTest(){test=null;outcomes.forEach(key=>{el("result-"+key).value="not-run";});el("test-notes").value="";el("test-status").textContent="尚无实测记录。网页分析不会自动证明目标端兼容。";}
  function persist(){
    try{const raw=JSON.stringify(report());if(new TextEncoder().encode(raw).length>model.MAX_FILE_BYTES)throw Error("快照超过本地容量上限");localStorage.setItem(KEY,raw);el("storage-status").textContent="已保存到本浏览器，刷新或断开后仍可比对。";}
    catch(error){el("storage-status").textContent="本地保存失败，请立即导出备份："+error.message;}
  }
  function download(value,name){const url=URL.createObjectURL(new Blob([JSON.stringify(value,null,2)],{type:"application/json"}));const a=document.createElement("a");a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
  function records(){return [...new Map([...compareDevices,singleDevice].filter(Boolean).map(rec=>[rec.device.id,rec])).values()];}
  function refreshDevices(){
    if(!el("board"))return;
    const list=records();
    for(const side of ["board","reference"]){const select=el(side),old=select.value;select.replaceChildren(new Option("选择已添加设备",""));list.forEach(rec=>select.add(new Option((rec.device.name||"未命名设备")+(rec.server?.connected?" · 已连接":" · 已断开"),rec.device.id)));if(list.some(rec=>rec.device.id===old))select.value=old;}
  }
  function privateServices(){const values=el("private-uuids").value.split(/[,\s，]+/).filter(Boolean);if(values.length>16)throw Error("额外授权最多16个服务UUID");return [...new Set(values.map(model.normalizeUuid))];}
  function slot(side,snapshot){
    const state=el(side+"-state"),summary=el(side+"-summary");
    if(!snapshot){state.textContent="未采集";state.className="badge muted";summary.textContent=side==="board"?"采集开发板当前安装的蓝牙能力。":"选择正常工作的功率计、心率带或骑行台。";return;}
    const count=snapshot.services.reduce((n,s)=>n+s.characteristics.length,0);
    const connected=live.get(side)?.device.gatt.connected===true;
    state.textContent=connected?"已保存 · 连接中":snapshot.coverage.enumeration==="complete"?"已保存":"已保存 · 部分采集";
    state.className="badge "+(snapshot.coverage.enumeration==="complete"?"ok":"warn");
    summary.innerHTML="<strong>"+escape(snapshot.name)+"</strong><span>"+snapshot.services.length+" 服务 / "+count+" 特征 / "+snapshot.samples.length+" 条通知</span><span>"+escape(snapshot.info.model||snapshot.info.manufacturer||"未读取型号")+" · "+escape(snapshot.info.firmware||"版本未知")+"</span><time>"+escape(new Date(snapshot.capturedAt).toLocaleString("zh-CN"))+"</time>";
  }
  function snapshotText(){
    slot("board",board);slot("reference",reference);
    el("snapshots").textContent=board&&reference?"两侧信息已保存；可以断开设备后分析差异。":"分别采集两侧，任一侧采集失败不会覆盖已保存的信息。";
    el("analyze").disabled=!board||!reference||busy;
    for(const side of ["board","reference"]){el("collect-"+side).disabled=busy;el("export-"+side).disabled=!(side==="board"?board:reference);}
    el("connection-mode").disabled=busy;el("clear-snapshots").disabled=busy;el("capture").disabled=busy;el("import").disabled=busy;
  }
  function clearResults(){el("results").replaceChildren();}
  function analyze(){
    if(!board||!reference)throw Error("请分别采集两侧设备，或导入两份信息");
    if(board.deviceKey&&board.deviceKey===reference.deviceKey)throw Error("两侧是同一设备，请重新选择");
    const result=model.analyze(board,reference,context());
    const differences=model.compareCapabilities(board,reference);
    const categories={service:"服务",characteristic:"特征",property:"属性",feature:"能力位",value:"读取值",identity:"身份",notification:"通知采集",payload:"通知格式",frequency:"通知频率"};
    const confirmed=result.findings.filter(f=>f.kind==="standard"&&f.side==="board");
    const unknown=differences.filter(d=>d.certainty==="unknown").length;
    let html="<div class='difference-overview'><div><b>"+differences.length+"</b><span>差异项</span></div><div><b>"+confirmed.length+"</b><span>开发板标准问题</span></div><div><b>"+unknown+"</b><span>需补采集</span></div></div><h3>差异与处理方向</h3>";
    if(!differences.length)html+="<p>本次授权范围内未发现静态差异，不代表骑行端已适配；握手、广播和长期稳定性仍需实测。</p>";
    else{
      html+="<div class='compat-table'><table class='difference-table'><colgroup><col style='width:25%'><col style='width:21%'><col style='width:21%'><col style='width:33%'></colgroup><thead><tr><th>项目</th><th>第三方设备</th><th>开发板</th><th>分析 / 补齐方向</th></tr></thead><tbody>";
      for(const d of differences)html+="<tr><td><span class='badge muted'>"+categories[d.category]+"</span><b>"+escape(d.label)+"</b></td><td>"+escape(d.reference)+"</td><td>"+escape(d.board)+"</td><td><span class='difference-certainty'>"+(d.certainty==="unknown"?"未采集 / 未授权，不判缺失":"已观察差异，不一定是故障")+"</span>"+escape(d.action)+"</td></tr>";
      html+="</tbody></table></div>";
    }
    html+="<details class='difference-findings' open><summary>标准检查与适配建议</summary>";
    const labels={standard:"标准问题",candidate:"待验证原因",unknown:"需补证据"};
    for(const f of result.findings)html+="<article class='compat-finding'><span class='badge "+(f.kind==="standard"?"err":"warn")+"'>"+labels[f.kind]+" · "+(f.side==="board"?"开发板":"第三方")+"</span><b>"+escape(f.title)+"</b><p class='compat-evidence'>"+escape(f.evidence)+"</p><p>"+escape(f.action)+"</p></article>";
    html+="</details><details><summary>通知解析与原始样本</summary>";
    for(const [side,snapshot] of [["reference",reference],["board",board]]){
      html+="<h3>"+(side==="board"?"开发板":"第三方设备")+"</h3>";
      for(const signal of result.signals[side])html+="<p><b>"+escape(model.label(signal.uuid))+"</b> · "+signal.count+" 条 / 格式异常 "+signal.errors+" / 未解析 "+signal.unparsed+" · 间隔中位 "+(signal.medianIntervalMs??"未知")+" ms · "+escape(signal.summary)+"</p>";
      for(const sample of snapshot.samples.slice(-8))html+="<p class='compat-evidence'>"+escape(model.label(sample.uuid))+" · "+escape(sample.raw)+"</p>";
    }
    html+="</details><p class='hint'>此页生成差异证据与修复建议，不自动修改固件。服务、能力和控制点须在固件中实现，复制名称或DIS不能补齐。</p>";
    el("results").innerHTML=html;status("发现 "+differences.length+" 项差异 · 待骑行端验证");return result;
  }
  function closeLive(side){const item=live.get(side);if(!item)return;item.listeners.forEach(([ch,fn])=>ch.removeEventListener("characteristicvaluechanged",fn));try{item.device.gatt.disconnect();}catch(_){}live.delete(side);}
  function releaseCapture(){generation++;for(const side of [...live.keys()])closeLive(side);busy=false;snapshotText();el("collection-progress").textContent="采集连接已释放，已保存的信息仍保留。";}
  function bounded(promise){let timer;return Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error("蓝牙操作超时，请重试；已保存信息保留")),6000);})]).finally(()=>clearTimeout(timer));}
  async function collect(side){
    if(busy)return;
    if(!navigator.bluetooth)throw Error("当前浏览器不支持网页蓝牙，请用电脑Chrome/Edge的HTTPS或localhost页面");
    if(flashSession?.isBusy())throw Error("请先结束串口会话");
    if(compatibilityHandoffActive())throw Error("请先结束骑行端实测，再采集");
    const optionalServices=[...new Set([...ALL_OPTIONAL_SERVICES,...privateServices()])];
    const token=++generation;busy=true;snapshotText();
    let device=null,held=null,succeeded=false,attempted=false;
    const started=Date.now();
    const check=()=>{if(token!==generation)throw Error("采集已取消");if(Date.now()-started>90000)throw Error("采集超过90秒，请减少私有服务后重试");assertBleConnection(device,epoch);if(held&&!held.server.connected)throw Error("采集中设备已断开，原快照保留");};
    const cleanup=()=>{if(device&&owners.get(device)===token){if(live.get(side)===held)closeLive(side);else disconnectPageGatt(device,epoch);}};
    let epoch=bleConnectionEpoch;
    try{
      el("collection-progress").textContent="请在浏览器中选择"+(side==="board"?"开发板":"第三方设备");
      device=await navigator.bluetooth.requestDevice({acceptAllDevices:true,optionalServices});
      if(token!==generation)return;
      const other=side==="board"?reference:board;
      if(other?.deviceKey===device.id)throw Error("两侧不能是同一台设备；请另选一台");
      if(el("connection-mode").value==="sequential"){
        setCompatibilityHandoff(true);setCompatibilityHandoff(false);
        for(const key of [...live.keys()])closeLive(key);
      }else closeLive(side);
      epoch=bleConnectionEpoch;pageGattPermissions.set(device,optionalServices);allowManualPageConnection();
      el("collection-progress").textContent="连接并读取服务、特征与属性…";
      attempted=true;owners.set(device,token);
      const connection=connectPageGatt(device,epoch);
      connection.then(()=>{if(token!==generation||!busy&&!succeeded)cleanup();},()=>{});
      const server=await bounded(connection);check();
      held={device,server,listeners:[]};live.set(side,held);
      device.addEventListener("gattserverdisconnected",()=>{if(live.get(side)===held&&!busy)snapshotText();},{once:true});
      const services=[],samples=[];let reads=0,subscriptions=0,partial=false;
      const gatt=await bounded(server.getPrimaryServices());check();
      if(gatt.length>64)throw Error("服务超过采集上限64个");
      for(const svc of gatt){
        check();const entry={uuid:svc.uuid,enumeration:"complete",characteristics:[]};
        try{
          const chars=await bounded(svc.getCharacteristics());check();
          if(chars.length>96)throw Error("特征超过采集上限96个");
          for(const ch of chars){
            check();const item={uuid:ch.uuid,properties:propFlags(ch),raw:null,readStatus:ch.properties.read?"skipped":"not-readable",readError:""};
            if(ch.properties.read&&reads<MAX_READS){reads++;try{const v=await bounded(ch.readValue());check();item.raw=hexDump(new Uint8Array(v.buffer,v.byteOffset,v.byteLength));item.readStatus="ok";}catch(error){check();item.readStatus="failed";item.readError=String(error.message).slice(0,256);}}
            entry.characteristics.push(item);
            if((ch.properties.notify||ch.properties.indicate)&&subscriptions<MAX_SUBSCRIPTIONS&&!ch.uuid.toLowerCase().startsWith("7a5a000")){
              const fn=event=>{if(token!==generation)return;const v=event.target.value;if(!v)return;const raw=hexDump(new Uint8Array(v.buffer,v.byteOffset,v.byteLength));if(raw.length>1536)return;samples.push({uuid:ch.uuid,at:Date.now(),raw});if(samples.length>model.MAX_SAMPLES)samples.shift();};
              ch.addEventListener("characteristicvaluechanged",fn);
              try{await bounded(ch.startNotifications());check();held.listeners.push([ch,fn]);subscriptions++;}catch(error){ch.removeEventListener("characteristicvaluechanged",fn);check();}
            }
          }
        }catch(error){check();entry.enumeration="partial";partial=true;}
        services.push(entry);
      }
      if(!services.length)throw Error("授权范围内没有可采集的服务；补充私有服务UUID再试");
      const hasConfig=services.some(s=>s.uuid.toLowerCase()===UUIDS.configService);
      if(side==="board"&&!hasConfig)throw Error("未观察到BikePower配置服务，不能确认为开发板；请核对选择和授权");
      if(side==="reference"&&hasConfig)throw Error("这台是BikePower开发板，请放到开发板侧，第三方侧另选正常设备");
      const info={};const dis=services.find(s=>s.uuid.toLowerCase()===UUIDS.disService);
      for(const [key,,uuid] of DIS_CHARS){const value=dis?.characteristics.find(c=>c.uuid.toLowerCase()===uuid);if(value?.readStatus==="ok")info[key]=new TextDecoder().decode(modelBytes(value.raw)).replace(/\0/g,"").trim();}
      for(let second=10;second>0;second--){check();el("collection-progress").textContent="正在采集"+(side==="board"?"开发板":"第三方")+"通知，剩余 "+second+" 秒 · "+samples.length+" 条";await new Promise(resolve=>setTimeout(resolve,SAMPLE_MS/10));}
      check();const next=model.capture({device,info,services,compatibilitySamples:samples,gattCoverage:{scope:"authorized",enumeration:partial?"partial":"complete",allowedServices:optionalServices}});
      if(side==="board")board=next;else reference=next;
      clearTest();clearResults();persist();succeeded=true;
      if(el("connection-mode").value==="sequential")closeLive(side);
      else if(board&&reference)el("connection-note").textContent=[...live.values()].filter(r=>r.device.gatt.connected).length===2?"两台设备当前均连接；快照已分别保存，可释放连接后分析。":"当前没有保持两台连接，已保存快照不受影响；可改为逐台采集。";
      el("collection-progress").textContent="已采集并保存"+(side==="board"?"开发板":"第三方设备")+" · "+samples.length+"条通知"+(samples.length?"":"（无通知，请使设备工作后重采）");status(board&&reference?"两侧已保存，可以分析差异":"已保存一侧，请采集另一侧","ok");
    }catch(error){
      if(token===generation){el("collection-progress").textContent=error.name==="NotFoundError"?"已取消选择，旧快照保留。":error.message+"；旧快照保留。";if(el("connection-mode").value==="simultaneous")el("connection-note").textContent="双连失败或采集失败，可改为逐台采集；已有快照保留。";status(error.message,"err");}
    }finally{
      if(!succeeded&&device&&attempted)cleanup();
      if(token===generation){busy=false;snapshotText();}
    }
  }
  function modelBytes(raw){return Uint8Array.from(raw.split(/\s+/).filter(Boolean),v=>parseInt(v,16));}
  function guarded(action){return async()=>{try{await action();}catch(error){status(error.message,"err");}};}
  function renderHandoff(){if(!el("handoff-status"))return;const active=compatibilityHandoffActive();el("handoff-status").textContent=active?"网页所有标签连接已暂停，刷新后仍暂停。完成骑行端实测后，再允许手动连接。":"网页连接尚未暂停。";el("resume").disabled=!active;}
  function applyImported(next){
    const nextBoard=next.snapshots.board||board,nextReference=next.snapshots.reference||reference;
    if(nextBoard?.deviceKey&&nextBoard.deviceKey===nextReference?.deviceKey)throw Error("两侧快照来自同一设备");
    if(nextBoard&&nextReference){model.analyze(nextBoard,nextReference,next.context);model.compareCapabilities(nextBoard,nextReference);}
    board=nextBoard;reference=nextReference;clearTest();
    for(const key of ["target","version","platform","role","stage"])el(key).value=next.context[key];el("reference-works").checked=next.context.referenceWorks;
    if(next.snapshots.board&&next.snapshots.reference){test=next.test;}else test=null;
    if(test){outcomes.forEach(key=>{el("result-"+key).value=test.outcomes[key];});el("test-notes").value=test.notes;el("test-status").textContent="已导入人工记录，不是自动实测证据。";}
    persist();snapshotText();clearResults();if(board&&reference)analyze();
  }
  function init(){
    for(const key of outcomes)for(const [value,label] of [["not-run","未测试"],["pass","通过"],["fail","失败"],["not-applicable","不适用"]])el("result-"+key).add(new Option(label,value));
    contextIds.forEach(id=>el(id).addEventListener(["reference-works","role","stage"].includes(id)?"change":"input",()=>{clearTest();clearResults();persist();status("场景已修改，快照保留，请重新分析");}));
    for(const side of ["board","reference"]){el("collect-"+side).addEventListener("click",guarded(()=>collect(side)));el("export-"+side).addEventListener("click",guarded(()=>{const value=side==="board"?board:reference;if(!value)throw Error("这一侧还未采集");download(model.makeReport(context(),side==="board"?value:null,side==="reference"?value:null,null),"bikepower-"+side+"-snapshot.json");}));}
    el("connection-mode").addEventListener("change",()=>{el("connection-note").textContent=el("connection-mode").value==="sequential"?"逐台连接，保存后释放；不依赖双连能力。":"需电脑与两台设备支持，失败时改为逐台；快照独立保留。";});
    el("release-capture").addEventListener("click",releaseCapture);
    el("clear-snapshots").addEventListener("click",()=>{releaseCapture();board=null;reference=null;clearTest();clearResults();try{localStorage.removeItem(KEY);}catch(_){}snapshotText();status("本地两份信息已清除");});
    el("capture").addEventListener("click",guarded(()=>{const list=records(),left=list.find(r=>r.device.id===el("board").value),right=list.find(r=>r.device.id===el("reference").value);if(!left||!right||left.device.id===right.device.id)throw Error("请选择两台不同设备");const a=model.capture(left),b=model.capture(right);board=a;reference=b;clearTest();persist();snapshotText();clearResults();status("已有两侧快照已保存");}));
    el("analyze").addEventListener("click",guarded(analyze));
    el("export").addEventListener("click",guarded(()=>{if(!board&&!reference)throw Error("请先采集设备信息");download(report(),"bikepower-compatibility.json");}));
    el("import").addEventListener("change",guarded(async()=>{const file=el("import").files[0];if(!file)return;try{if(file.size>model.MAX_FILE_BYTES)throw Error("文件超过1MiB");const next=model.importReport(await file.text());if(!next.snapshots.board&&!next.snapshots.reference)throw Error("报告没有设备信息");applyImported(next);status("已导入保存的设备信息");}finally{el("import").value="";}}));
    el("handoff").addEventListener("click",()=>{releaseCapture();setCompatibilityHandoff(true);renderHandoff();});el("resume").addEventListener("click",()=>{setCompatibilityHandoff(false);renderHandoff();});
    el("save-test").addEventListener("click",guarded(()=>{if(!board||!reference)throw Error("请先保存两侧信息");if(!compatibilityHandoffActive())throw Error("先释放网页连接再记录骑行端实测");const next={recordedAt:new Date().toISOString(),outcomes:{},notes:el("test-notes").value};outcomes.forEach(key=>{next.outcomes[key]=el("result-"+key).value;});test=model.makeReport(context(),board,reference,next).test;persist();el("test-status").textContent="人工记录已绑定当前场景与快照。";}));
    try{const raw=localStorage.getItem(KEY);if(raw){const next=model.importReport(raw);board=next.snapshots.board;reference=next.snapshots.reference;for(const key of ["target","version","platform","role","stage"])el(key).value=next.context[key];el("reference-works").checked=next.context.referenceWorks;test=next.test;el("storage-status").textContent="已恢复本浏览器保存的快照。";}}catch(error){el("storage-status").textContent="保存数据无法恢复，请导入备份："+error.message;}
    if(test){outcomes.forEach(key=>{el("result-"+key).value=test.outcomes[key];});el("test-notes").value=test.notes;el("test-status").textContent="已恢复人工记录，不是自动实测证据。";}
    snapshotText();refreshDevices();renderHandoff();
    window.addEventListener("beforeunload",()=>{for(const side of [...live.keys()])closeLive(side);});
  }
  window.BikePowerCompatibilityUI={report,refreshDevices,privateServices,renderHandoff,collect,releaseCapture};
  window.addEventListener("load",init);
})();
