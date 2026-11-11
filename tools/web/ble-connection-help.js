(function(root,factory){
  const api=factory();
  if(typeof module==="object"&&module.exports)module.exports=api;
  else root.BikePowerConnectionHelp=api;
})(typeof globalThis!=="undefined"?globalThis:this,function(){
  "use strict";
  const pairingSteps=[
    "仅当刚全量刷机、恢复出厂，或已明确提示配对信息丢失时，处理这台 BikePower 的旧配对；不要清空全部蓝牙设备。",
    "Mac：系统设置 → 蓝牙 → BikePower 右侧详情 → 忽略此设备。Windows：设置 → 蓝牙和设备 → 设备 → BikePower → 删除设备。",
    "回到桌面 Chrome / Edge 的本站页面，点击“扫描并连接设备”，选中当前开发板，再点击选择器中的“配对/连接”。普通传感器读取不必先在系统中配对。",
    "若仍失败，保留错误与事件日志交给维护人员核对两端绑定；不要整片擦除、恢复出厂或自动改 MAC。"
  ];
  const catalog={
    cancelled:{title:"尚未选择蓝牙设备",summary:"已取消选择或没有选中设备，尚未建立连接；这不代表开发板故障。",steps:["需要连接时重新点击“扫描并连接设备”，选中当前 BikePower 后点击“配对/连接”。","若列表为空，检查供电和广播、靠近电脑；只断开确实占用这台设备的 App 或其他页面。刷完后等待启动，仍无广播再按 EN/RST 或重新上电（不是恢复出厂）。"],severity:"warn"},
    permission:{title:"蓝牙权限未允许",summary:"蓝牙权限未允许：页面或系统没有允许此次访问；API 可用不等于已获授权。",steps:["使用桌面 Chrome / Edge，在 HTTPS 页面主动点击连接；不要从嵌入页面或后台自动发起设备选择。","检查地址栏网站信息中的蓝牙设备权限；Mac 还需检查系统设置 → 隐私与安全性 → 蓝牙，是否允许当前浏览器。","允许后回到本站重新选择设备；公司策略限制时联系管理员，不绕过权限。"]},
    environment:{title:"当前环境不支持网页蓝牙",summary:"当前页面不是安全上下文，或浏览器没有 Web Bluetooth。",steps:["用桌面 Chrome / Edge 直接打开本站 HTTPS 地址，更新浏览器；不要仅根据浏览器名称判断支持。","Safari、Firefox 和 iOS 浏览器不能使用本工具的网页蓝牙。手机配置请连接开发板热点后访问 192.168.4.1；手册和演示仍可查看。"]},
    radio:{title:"电脑蓝牙不可用",summary:"系统报告蓝牙适配器未开启或不可用。",steps:["在电脑系统设置中开启蓝牙，检查适配器是否正常。","保持开发板上电并靠近电脑，然后主动重新扫描；先不修改开发板配置或 MAC。"]},
    pairing:{title:"旧配对信息已失效",summary:"错误明确提示配对信息丢失或密钥不一致。电脑保留的旧配对可能与刷机后的开发板不一致。",steps:pairingSteps},
    busy:{title:"上一连接仍在收尾",summary:"上一次底层蓝牙操作尚未结束，页面不会重复建链或抢占设备。",steps:["等待当前操作收尾，不要连续点击连接，也不要同时开多个窗口重试。","长时间不结束时，保留日志，关闭这一个设备页面，确认设备恢复广播后重新打开连接。"]},
    paused:{title:"网页蓝牙已暂停",summary:"目标 App 实测期间，本站暂停蓝牙以避免抢占；不是连接故障。",steps:["完成目标 App 实测，释放该 App 对设备的连接。","点击“结束实测，恢复网页连接”，再主动选择设备；恢复不会替你静默授权。"],severity:"warn"},
    serial:{title:"刷写或串口会话尚未结束",summary:"安装弹窗或串口日志会话仍占用当前工作流程。",steps:["等待刷写真正完成并关闭安装/日志弹窗，确认串口释放。","设备启动后再连接蓝牙；写入 100%、关闭弹窗本身都不等于刷写成功。"]},
    timeout:{title:"蓝牙握手超时",summary:"页面已停止本次读取，但底层连接可能仍在收尾；超时本身不能证明设备缺少服务或被其他 App 占用。",steps:["等待收尾后重试，保持设备上电并靠近电脑；只释放确实占用同一台设备的 App/页面。","若刚全量刷机且此前已系统配对，按“刷机后旧配对”步骤仅忽略/删除 BikePower，再重新选择。","仍超时，导出事件日志并提供浏览器/系统、固件版本、是否刚刷机及是否能扫描到设备。"]},
    disconnected:{title:"蓝牙链路已断开",summary:"读取或连接过程中链路关闭；断开不等于服务缺失，历史数值不再代表实时输出。",steps:["检查供电、距离与设备是否复位；保存配置后的预期 GATT 重建请等待设备恢复。","确认这台设备恢复广播后重新连接；系统有时会延迟释放旧链路，稍候仍扫不到再按 EN/RST 或重新上电。","若刚全量刷机且曾系统配对，条件性检查旧配对；没有该证据时不要直接清绑定。"]},
    discovery:{title:"蓝牙服务读取未完成",summary:"链路或服务枚举异常，尚不能判定开发板缺少运动服务，也不能直接判定为系统缓存。",steps:["重新选择当前设备，并检查广播开关、骑行/跑步模式与实际来源是否符合预期。","若连接稳定但重连后仍显示旧服务表，保留日志由维护人员核对 GATT 缓存与 Service Changed；不要用改名或换 MAC 代替诊断。","DIS 厂商/序列号等可选信息读取不完整时，运动通知仍可能正常；以服务与通知的实际结果为准。"]},
    unsupported:{title:"未发现可用运动服务",summary:"本次服务枚举完成后，没有发现已授权的运动或参考服务；不要套用到枚举失败或断连。",steps:["核对所选设备、固件版本及广播/运动模式，重新选择并授权。","默认“踏频随功率”时可不暴露独立 CSC，踏频由 CPS/FTMS 获取；跑步 RSC、骑行 FTMS 与心率 HRS 也取决于模式和开关。","设备本来不支持这些服务时换相应传感器；不要因一个可选服务缺失就重刷固件。"]},
    notifications:{title:"已连接但数据未更新",summary:"GATT 连接不等于通知已订阅或数据新鲜；没有通知时不要把旧值当实时值。",steps:["查看事件日志中 CPS/HRS/FTMS 的订阅结果及最后通知时间，检查对应输出开关。真实功率计需先唤醒并核对“实际来源”。","默认踏频由功率连接获取；独立 CSC 缺失不等于没有踏频。骑鬥同设备功率/踏频正常但不接 HRS 时，用支持多服务的客户端核对，必要时使用独立心率带。","仍不更新，导出报告后重连；不要先改 MAC 或清空配置。"]},
    authorized:{title:"需要重新点选设备",summary:"当前浏览器不支持已授权设备列表，或该站点没有可用授权；这不等于蓝牙硬件故障。",steps:["点击“扫描并连接设备”，在选择器选中设备并确认。","刷新、换浏览器/窗口或授权失效后可能需要再次点选；浏览器不支持该能力时，不承诺免弹窗自动重连。"],severity:"warn"},
    generic:{title:"蓝牙连接失败，原因未确定",summary:"浏览器只返回通用连接失败，无法仅凭此错误区分旧配对、供电、距离、链路占用或系统问题。",steps:["先检查开发板供电、靠近电脑并确认能扫描到当前设备；仅释放确实占用它的 App/其他页面，再重试。","如果刚刷入 BikePower 全量合并固件或恢复出厂，且以前在系统里配对过：Mac 在系统设置 → 蓝牙 → BikePower 详情 → 忽略此设备；Windows 在设置 → 蓝牙和设备 → 设备 → BikePower → 删除设备。然后回本站重新点选连接。","未刚刷机、也无明确配对错误时，不要直接清绑定；仍失败请保留错误与事件日志，提供系统/浏览器、固件版本和刷机方式。不要整片擦除或自动改 MAC。"]}
  };
  function classify(error,context){
    context=context||{};
    const name=error&&error.name||"";
    const message=String(error&&error.message!==undefined?error.message:error||"");
    let category=context.category;
    if(!catalog[category]){
      if(/peer removed pairing information|pin.?or.?key missing|pin\/key missing|配对信息.*(丢失|失效)|密钥.*(丢失|不一致)/i.test(message))category="pairing";
      else if(context.secure===false||context.bluetooth===false||/环境不支持网页蓝牙|无 Web Bluetooth/i.test(message))category="environment";
      else if(/bluetooth.*(powered off|not available|unavailable)|蓝牙适配器.*(关闭|不可用)/i.test(message))category="radio";
      else if(/仍在建立或收尾|仍在收尾/.test(message))category="busy";
      else if(/网页蓝牙已暂停|目标端实测|实测期间.*暂停/.test(message))category="paused";
      else if(/串口.*(期间|尚未关闭|会话)|安装弹窗/.test(message))category="serial";
      else if(/getDevices|已授权设备列表|没有.*已授权设备/.test(message))category="authorized";
      else if((name==="NotFoundError"&&context.phase==="picker")||/已取消蓝牙设备选择|用户已取消本次蓝牙连接|User cancelled/i.test(message))category="cancelled";
      else if(name==="SecurityError"||name==="NotAllowedError"||/蓝牙权限|浏览器未允许蓝牙访问|not allowed to access/i.test(message))category="permission";
      else if(/超时|timed? ?out/i.test(message))category="timeout";
      else if(/GATT.*disconnected|链路.*断开|蓝牙.*断开|已断开.*读取|连接已断开/i.test(message))category="disconnected";
      else if(/服务发现失败|service discovery failed|服务枚举失败/i.test(message))category="discovery";
      else if(/未提供运动服务|已过滤/.test(message))category="unsupported";
      else category="generic";
    }
    const help=catalog[category];
    return {category,title:help.title,summary:help.summary,steps:help.steps.slice(),severity:help.severity||"error",technical:message};
  }
  function text(help){return help.title+"。"+help.summary+" "+help.steps.join(" ");}
  return {classify,text,catalog,pairingSteps};
});
