/* 诊断交互：固定快照、人工记录与蓝牙连接交接。 */
(function(){
  "use strict";
  const model=window.BikePowerCompatibility;
  const outcomes=["discovery","connection","data","reconnect","control"];
  const contextIds=["target","version","platform","role","stage","reference-works"];
  let board=null,reference=null,test=null;
  const el=id=>document.getElementById("compat-"+id);
  const escape=value=>escapeHtml(String(value===null||value===undefined?"":value));
  function context(){
    return model.validateContext({target:el("target").value,version:el("version").value,platform:el("platform").value,
      role:el("role").value,stage:el("stage").value,referenceWorks:el("reference-works").checked});
  }
  function report(){return model.makeReport(context(),board,reference,test);}
  function status(text,kind="warn"){el("status").textContent=text;el("status").className="badge "+kind;}
  function clearTest(){
    test=null;
    outcomes.forEach(key=>{el("result-"+key).value="not-run";});
    el("test-notes").value="";
    el("test-status").textContent="尚无实测记录。网页分析不会自动证明目标端兼容。";
  }
  function records(){
    return [...new Map([...compareDevices,singleDevice].filter(Boolean).map(rec=>[rec.device.id,rec])).values()];
  }
  function refreshDevices(){
    if(!el("board"))return;
    const list=records();
    for(const side of ["board","reference"]){
      const select=el(side),old=select.value;
      select.replaceChildren(new Option("选择已添加设备",""));
      list.forEach(rec=>select.add(new Option((rec.device.name||"未命名设备")+(rec.server&&rec.server.connected?" · 已连接":" · 已断开"),rec.device.id)));
      if(list.some(rec=>rec.device.id===old))select.value=old;
    }
  }
  function privateServices(){
    const values=el("private-uuids").value.split(/[,\s，]+/).filter(Boolean);
    if(values.length>16)throw new Error("额外授权最多 16 个服务 UUID");
    return [...new Set(values.map(model.normalizeUuid))];
  }
  function snapshotText(){
    el("snapshots").textContent=board&&reference?
      "固定快照："+board.name+"（"+board.services.length+" 服务 / "+board.samples.length+" 条通知） ↔ "+reference.name+
      "（"+reference.services.length+" 服务 / "+reference.samples.length+" 条通知）。采集于 "+new Date(board.capturedAt).toLocaleString("zh-CN"):"尚未冻结两台设备的快照。";
  }
  function analyze(){
    if(!board||!reference)throw new Error("请先冻结两台设备快照或导入报告");
    const result=model.analyze(board,reference,context());
    const labels={standard:"标准问题",candidate:"候选原因",unknown:"采集未知"};
    const sideLabels={board:"开发板",reference:"参考设备"};
    let html="<h3>诊断建议</h3><p class='hint'>以下来自固定快照，不能证明目标端实际兼容。浏览器报告中的未知项需要补充采集。</p>";
    for(const kind of ["standard","candidate","unknown"]){
      const findings=result.findings.filter(f=>f.kind===kind);
      html+="<h3>"+labels[kind]+" · "+findings.length+"</h3>";
      if(!findings.length)html+="<p class='hint'>本次快照未发现此类问题。</p>";
      for(const finding of findings)html+="<article class='compat-finding'><span class='badge "+(kind==="standard"?"err":"warn")+"'>"+labels[kind]+" · "+sideLabels[finding.side]+"</span><b>"+escape(finding.title)+"</b><p class='compat-evidence'>"+escape(finding.evidence)+"</p><p>"+escape(finding.action)+"</p></article>";
    }
    html+="<h3>通知样本</h3><div class='compat-table'><table><thead><tr><th>设备 / 信号</th><th>样本 / 格式异常</th><th>间隔中位 / 最大</th><th>最新合法值</th></tr></thead><tbody>";
    for(const side of ["board","reference"]){
      for(const signal of result.signals[side])html+="<tr><td>"+sideLabels[side]+" · "+escape(model.label(signal.uuid))+"</td><td>"+signal.count+" / "+(signal.unparsed?"未解析 "+signal.unparsed+" 条":signal.errors)+"</td><td>"+(signal.medianIntervalMs===null?"未知":signal.medianIntervalMs+" / "+signal.maxIntervalMs+" ms")+"</td><td>"+escape(signal.summary)+"</td></tr>";
    }
    html+="</tbody></table></div><details><summary>服务、属性与读取值差异 · "+result.differences.length+"</summary><div class='compat-table'><table><thead><tr><th>服务 / 特征</th><th>开发板</th><th>参考设备</th></tr></thead><tbody>";
    for(const difference of result.differences)html+="<tr><td>"+escape(difference.label)+"</td><td>"+escape(difference.board)+"</td><td>"+escape(difference.reference)+"</td></tr>";
    html+="</tbody></table></div></details>";
    el("results").innerHTML=html;
    status(result.verdict);
    return result;
  }
  function guarded(action){return async()=>{try{await action();}catch(error){status(error.message,"err");}};}
  function renderHandoff(){
    if(!el("handoff-status"))return;
    const active=compatibilityHandoffActive();
    el("handoff-status").textContent=active?"本站所有标签页的网页连接已暂停，刷新后仍保持暂停。请在目标端实测；结束后先关闭目标端连接，再手动连接网页。":"网页连接尚未暂停。结束实测后仅允许手动连接，避免抢占目标端。";
    el("resume").disabled=!active;
  }
  function init(){
    for(const key of outcomes){
      for(const [value,label] of [["not-run","未测试"],["pass","通过"],["fail","失败"],["not-applicable","不适用"]])
        el("result-"+key).add(new Option(label,value));
    }
    contextIds.forEach(id=>el(id).addEventListener(id==="reference-works"||id==="role"||id==="stage"?"change":"input",()=>{
      clearTest();el("results").replaceChildren();status("场景已修改，请重新分析固定快照");
    }));
    el("capture").addEventListener("click",guarded(()=>{
      if(!context().target.trim())throw new Error("请填写目标软件或码表");
      const list=records(),left=list.find(rec=>rec.device.id===el("board").value),right=list.find(rec=>rec.device.id===el("reference").value);
      if(!left||!right||left===right)throw new Error("请选择两台不同设备");
      const nextBoard=model.capture(left),nextReference=model.capture(right);
      board=nextBoard;reference=nextReference;clearTest();snapshotText();el("results").replaceChildren();
      status("快照已冻结，请分析");
    }));
    el("analyze").addEventListener("click",guarded(analyze));
    el("export").addEventListener("click",guarded(()=>{
      if(!board||!reference)throw new Error("请先冻结两台设备快照");
      const blob=new Blob([JSON.stringify(report(),null,2)],{type:"application/json"});
      const url=URL.createObjectURL(blob),link=document.createElement("a");
      link.href=url;link.download="bikepower-compatibility-"+new Date().toISOString().slice(0,10)+".json";
      link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
    }));
    el("import").addEventListener("change",guarded(async()=>{
      const file=el("import").files[0];if(!file)return;
      try{
        if(file.size>model.MAX_FILE_BYTES)throw new Error("报告文件大小超过 1 MiB");
        const next=model.importReport(await file.text());
        if(!next.snapshots.board||!next.snapshots.reference)throw new Error("报告需包含两台设备快照");
        board=next.snapshots.board;reference=next.snapshots.reference;clearTest();
        for(const key of ["target","version","platform","role","stage"])el(key).value=next.context[key];
        el("reference-works").checked=next.context.referenceWorks;
        test=next.test;
        if(test){outcomes.forEach(key=>{el("result-"+key).value=test.outcomes[key];});el("test-notes").value=test.notes;
          el("test-status").textContent="已导入人工记录，请核对目标版本；导入数据不是自动实测证据。";}
        snapshotText();analyze();status("已导入固定快照 · 待目标端验证");
      }finally{el("import").value="";}
    }));
    el("handoff").addEventListener("click",()=>{setCompatibilityHandoff(true);renderHandoff();});
    el("resume").addEventListener("click",()=>{setCompatibilityHandoff(false);renderHandoff();});
    el("save-test").addEventListener("click",guarded(()=>{
      if(!board||!reference)throw new Error("请先冻结两台设备快照");
      if(!compatibilityHandoffActive())throw new Error("请先释放网页连接，再记录目标端实测");
      const next={recordedAt:new Date().toISOString(),outcomes:{},notes:el("test-notes").value};
      outcomes.forEach(key=>{next.outcomes[key]=el("result-"+key).value;});
      test=model.makeReport(context(),board,reference,next).test;
      el("test-status").textContent="人工实测记录已绑定当前场景与快照，请导出 JSON 保存。";
      status("人工记录已保存 · 请核对目标端证据","ok");
    }));
    refreshDevices();renderHandoff();
  }
  window.BikePowerCompatibilityUI={report,refreshDevices,privateServices,renderHandoff};
  window.addEventListener("load",init);
})();
