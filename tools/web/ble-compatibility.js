/* 固定蓝牙快照与兼容性规则；不依赖 DOM，不向设备写入。 */
(function(root, factory){
  const api=factory();
  if(typeof module==="object"&&module.exports)module.exports=api;
  else root.BikePowerCompatibility=api;
})(typeof globalThis!=="undefined"?globalThis:this,function(){
  "use strict";
  const SCHEMA="bikepower.compatibility.v1";
  const MAX_FILE_BYTES=1024*1024;
  const MAX_SAMPLES=120;
  const INFO_KEYS=["manufacturer","model","serial","hardware","firmware","software"];
  const ROLES=["power","cadence","heart-rate","trainer","running"];
  const STAGES=["discover","connect","disconnect","data","control"];
  const OUTCOMES=["not-run","pass","fail","not-applicable"];
  const PROPERTY_NAMES=["read","write","write-wr","notify","indicate","broadcast","signed"];
  const FEATURE_LENGTHS={"2a65":4,"2a5c":2,"2a54":2,"2acc":8};
  const SERVICE_RULES={
    "1818":[["2a63","notify"],["2a65","read"],["2a5d","read"]],
    "1816":[["2a5b","notify"],["2a5c","read"]],
    "180d":[["2a37","notify"]],
    "1814":[["2a53","notify"],["2a54","read"]],
    "1826":[["2acc","read"]],
  };
  const LABELS={"1818":"CPS 功率","1816":"CSC 踏频","180d":"HRS 心率","1814":"RSC 跑步","1826":"FTMS",
    "2a63":"CPS Measurement","2a65":"CPS Feature","2a5d":"Sensor Location",
    "2a5b":"CSC Measurement","2a5c":"CSC Feature","2a37":"Heart Rate Measurement",
    "2a53":"RSC Measurement","2a54":"RSC Feature","2acc":"FTMS Feature",
    "2ad2":"Indoor Bike Data","2acd":"Treadmill Data","2ad9":"FTMS Control Point",
    "2ad4":"Supported Speed Range","2ad8":"Supported Power Range","2ada":"Fitness Machine Status"};

  function text(value,max=256){
    if(value===null||value===undefined)return "";
    if(typeof value!=="string"||value.length>max)throw new Error("文本类型或长度不合法");
    return value;
  }
  function normalizeUuid(value){
    const raw=text(value,36).trim().toLowerCase();
    if(/^[0-9a-f]{4}$/.test(raw))return "0000"+raw+"-0000-1000-8000-00805f9b34fb";
    if(/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(raw))return raw;
    throw new Error("UUID 格式不合法");
  }
  function shortUuid(value){
    const full=normalizeUuid(value);
    return /^0000[0-9a-f]{4}-0000-1000-8000-00805f9b34fb$/.test(full)?full.slice(4,8):full;
  }
  function label(uuid){const short=shortUuid(uuid);return (LABELS[short]||short)+" · "+short;}
  function hexBytes(raw){
    if(typeof raw!=="string"||raw.length>1536||!/^(?:[0-9a-f]{2}\s*)*$/i.test(raw))throw new Error("原始数据不是合法十六进制");
    const cleaned=raw.replace(/\s/g,"");
    if(cleaned.length>1024)throw new Error("原始数据长度超过上限");
    return Uint8Array.from(cleaned.match(/../g)||[],pair=>parseInt(pair,16));
  }
  function canonicalHex(raw){
    if(raw===null||raw===undefined)return null;
    return Array.from(hexBytes(raw),n=>n.toString(16).padStart(2,"0")).join(" ");
  }
  function enumeration(value){return ["complete","partial","failed","unknown"].includes(value)?value:"unknown";}
  function boundedArray(value,max,labelText){
    if(!Array.isArray(value)||value.length>max)throw new Error(labelText+"数量超过上限或类型不合法");
    return value;
  }
  function unique(items,key){
    if(new Set(items.map(item=>item[key])).size!==items.length)throw new Error("存在重复 "+key);
    return items;
  }
  function validateSnapshot(value){
    if(!value||typeof value!=="object")throw new Error("快照格式不合法");
    const id=text(value.id);
    if(!id)throw new Error("快照缺少身份");
    const capturedAt=text(value.capturedAt,64);
    if(!Number.isFinite(Date.parse(capturedAt)))throw new Error("快照时间不合法");
    const info={};
    for(const key of INFO_KEYS)info[key]=text(value.info&&value.info[key]);
    const coverage=value.coverage||{};
    const allowedServices=boundedArray(coverage.allowedServices||[],64,"授权服务").map(normalizeUuid);
    const services=unique(boundedArray(value.services,64,"服务").map(service=>({
      uuid:normalizeUuid(service.uuid),
      enumeration:enumeration(service.enumeration),
      characteristics:unique(boundedArray(service.characteristics,96,"特征").filter(ch=>ch.uuid).map(ch=>{
        const properties=text(ch.properties).split("|").filter(Boolean);
        if(properties.some(p=>!PROPERTY_NAMES.includes(p)))throw new Error("特征属性不合法");
        const raw=canonicalHex(ch.raw);
        const readStatus=["ok","failed","skipped","not-readable","unknown"].includes(ch.readStatus)
          ?ch.readStatus:(raw!==null?"ok":"unknown");
        if(readStatus==="ok"&&raw===null)throw new Error("特征读取状态与原始值不一致");
        return {uuid:normalizeUuid(ch.uuid),properties:[...new Set(properties)].sort().join("|"),
          raw,readStatus,readError:text(ch.readError)};
      }),"uuid"),
    })),"uuid");
    const samples=boundedArray(value.samples||[],MAX_SAMPLES,"通知样本").map(sample=>{
      if(typeof sample.at!=="number"||!Number.isFinite(sample.at)||sample.at<0)throw new Error("通知时间不合法");
      return {uuid:normalizeUuid(sample.uuid),at:sample.at,raw:canonicalHex(sample.raw)||""};
    });
    return {id,name:text(value.name),capturedAt,info,
      coverage:{scope:coverage.scope==="authorized"?"authorized":"unknown",
        enumeration:enumeration(coverage.enumeration),allowedServices:[...new Set(allowedServices)].sort()},
      services,samples};
  }
  function capture(record){
    if(!record||!record.device)throw new Error("请先连接并选择设备");
    return validateSnapshot({
      id:record.device.id+"@"+Date.now()+"-"+Math.random().toString(16).slice(2,10),
      name:record.device.name||"未命名设备",capturedAt:new Date().toISOString(),info:record.info||{},
      coverage:record.gattCoverage||{scope:"unknown",enumeration:"unknown",allowedServices:[]},
      services:(record.services||[]).map(service=>({
        uuid:service.uuid,enumeration:service.enumeration||"unknown",
        characteristics:(service.characteristics||[]).filter(ch=>ch.uuid).map(ch=>({
          uuid:ch.uuid,properties:ch.properties||"",readStatus:ch.readStatus,
          readError:ch.readError||"",raw:ch.raw===undefined?null:ch.raw,
        })),
      })),
      samples:(record.compatibilitySamples||[]).slice(-MAX_SAMPLES),
    });
  }
  function validateContext(value){
    value=value||{};
    return {target:text(value.target,100),version:text(value.version,100),platform:text(value.platform,100),
      role:ROLES.includes(value.role)?value.role:"power",
      stage:STAGES.includes(value.stage)?value.stage:"connect",referenceWorks:value.referenceWorks===true};
  }
  function makeReport(context,board,reference,test){
    const result={schema:SCHEMA,createdAt:new Date().toISOString(),context:validateContext(context),
      snapshots:{board:board?validateSnapshot(board):null,reference:reference?validateSnapshot(reference):null},test:null};
    if(test){
      const outcomes={};
      for(const key of ["discovery","connection","data","reconnect","control"]){
        if(!OUTCOMES.includes(test.outcomes&&test.outcomes[key]))throw new Error("实测结果格式不合法");
        outcomes[key]=test.outcomes[key];
      }
      const recordedAt=text(test.recordedAt,64);
      if(!Number.isFinite(Date.parse(recordedAt)))throw new Error("实测时间不合法");
      result.test={recordedAt,outcomes,notes:text(test.notes,2000),
        source:test.source==="imported-user-record"?"imported-user-record":"user-recorded"};
    }
    return result;
  }
  function importReport(raw){
    if(typeof raw!=="string"||new TextEncoder().encode(raw).length>MAX_FILE_BYTES)throw new Error("报告文件大小超过 1 MiB");
    let value;
    try{value=JSON.parse(raw);}catch(error){throw new Error("报告 JSON 格式不合法");}
    if(!value||value.schema!==SCHEMA)throw new Error("报告版本不支持");
    if(!value.snapshots)throw new Error("报告缺少快照");
    const report=makeReport(value.context,value.snapshots.board,value.snapshots.reference,value.test);
    if(report.test)report.test.source="imported-user-record";
    return report;
  }
  function decodePacket(uuid,raw){
    const short=shortUuid(uuid),bytes=hexBytes(raw),view=new DataView(bytes.buffer);
    let offset=0,flags=0;
    const need=n=>{if(bytes.length<offset+n)throw new Error("通知长度不足");};
    const u8=()=>{need(1);return view.getUint8(offset++);};
    const u16=()=>{need(2);const value=view.getUint16(offset,true);offset+=2;return value;};
    const i16=()=>{need(2);const value=view.getInt16(offset,true);offset+=2;return value;};
    const skip=n=>{need(n);offset+=n;};
    const result={};
    if(short==="2a63"){
      flags=u16();result.flags=flags;result.powerW=i16();
      for(const [bit,len] of [[0,1],[2,2],[4,6],[5,4],[6,4],[7,4],[8,3],[9,2],[10,2],[11,2]])
        if(flags&(1<<bit))skip(len);
    }else if(short==="2a5b"){
      flags=u8();result.flags=flags;
      if(flags&1)skip(6);
      if(flags&2){result.crankRevolutions=u16();result.crankEventTime=u16();}
    }else if(short==="2a37"){
      flags=u8();result.flags=flags;result.heartRateBpm=flags&1?u16():u8();
      if(flags&8)skip(2);
      if(flags&16){while(offset<bytes.length)skip(2);}
    }else if(short==="2a53"){
      flags=u8();result.flags=flags;result.speedKmh=u16()/256*3.6;result.cadenceSpm=u8();
      if(flags&1)result.strideCm=u16();
      if(flags&2){need(4);result.distanceM=view.getUint32(offset,true)/10;offset+=4;}
      result.running=!!(flags&4);
    }else if(short==="2ad2"){
      flags=u16();result.flags=flags;
      if(!(flags&1))result.speedKmh=u16()/100;
      if(flags&2)skip(2);
      if(flags&4)result.cadenceRpm=u16()/2;
      for(const [bit,len] of [[3,2],[4,3],[5,2]])if(flags&(1<<bit))skip(len);
      if(flags&64)result.powerW=i16();
      if(flags&128)skip(2);
      if(flags&256)skip(5);
      if(flags&512)result.heartRateBpm=u8();
      for(const [bit,len] of [[10,1],[11,2],[12,2]])if(flags&(1<<bit))skip(len);
    }else if(short==="2acd"){
      flags=u16();result.flags=flags;
      if(!(flags&1))result.speedKmh=u16()/100;
      if(flags&2)skip(2);
      if(flags&4){need(3);result.distanceM=view.getUint16(offset,true)+(view.getUint8(offset+2)<<16);offset+=3;}
      for(const [bit,len] of [[3,4],[4,4],[5,1],[6,1],[7,5]])if(flags&(1<<bit))skip(len);
      if(flags&256)result.heartRateBpm=u8();
      if(flags&512)skip(1);
      if(flags&1024)result.elapsedTimeS=u16();
      if(flags&2048)skip(2);
      if(flags&4096)skip(4);
    }else if(short==="2ad9"){
      result.response=u8();result.opcode=u8();result.result=u8();
      if(result.response!==0x80)throw new Error("控制点响应不是 Response Code");
    }else{
      return {raw:canonicalHex(raw),supported:false};
    }
    if(offset!==bytes.length&&!["2a63","2a5b","2a53","2ad2","2acd"].includes(short))throw new Error("通知长度与 flags 不一致");
    return result;
  }
  function featureSummary(uuid,raw){
    const short=shortUuid(uuid);
    if(!FEATURE_LENGTHS[short]||raw===null)return raw||"未读取";
    const bytes=hexBytes(raw);
    if(bytes.length!==FEATURE_LENGTHS[short])return "长度异常："+bytes.length+" B";
    const view=new DataView(bytes.buffer),bits=bytes.length>=4?view.getUint32(0,true):view.getUint16(0,true);
    if(short==="2a65")return "CPS Feature 0x"+bits.toString(16)+" · "+(bits&8?"支持曲柄转数":"不声明曲柄转数")
      +" · "+(["分布式系统未定义","非分布式总功率","分布式系统","保留值"][(bits>>>20)&3]);
    if(short==="2a5c")return "CSC Feature 0x"+bits.toString(16)+" · "+(bits&2?"曲柄":"")+(bits&1?" / 轮速":"");
    if(short==="2a54")return "RSC Feature 0x"+bits.toString(16)+" · 步幅/距离/跑步状态按能力位";
    const target=view.getUint32(4,true);
    return "machine=0x"+bits.toString(16)+" / target=0x"+target.toString(16)
      +(target&8?" · 支持目标功率":" · 不声明目标功率");
  }
  function signalSummary(snapshot,findings,side){
    const groups=new Map();
    for(const sample of snapshot.samples){
      if(!groups.has(sample.uuid))groups.set(sample.uuid,[]);
      groups.get(sample.uuid).push(sample);
    }
    const result=[];
    for(const [uuid,samples] of groups){
      const sorted=samples.slice().sort((a,b)=>a.at-b.at);
      const intervals=sorted.slice(1).map((s,i)=>s.at-sorted[i].at).sort((a,b)=>a-b);
      let last=null,errors=0,unparsed=0,firstError=null;
      for(const sample of sorted){
        try{const decoded=decodePacket(uuid,sample.raw);if(decoded.supported===false)unparsed++;else last=decoded;}
        catch(error){errors++;if(!firstError)firstError={sample,error};}
      }
      if(firstError)findings.push({kind:"standard",code:"payload-"+shortUuid(uuid),side,
        title:label(uuid)+" 通知格式异常",evidence:firstError.sample.raw+" · "+firstError.error.message,
        action:"按原始 flags 和实际长度核对编码；修改后重新采集并在目标端测试。"});
      if(unparsed)findings.push({kind:"unknown",code:"payload-unknown-"+shortUuid(uuid),side,
        title:label(uuid)+" 通知尚未解析",evidence:unparsed+" 条原始样本未校验格式",
        action:"提供该协议定义后解析，未解析不能视为格式正确。"});
      const units={powerW:"W",heartRateBpm:"bpm",cadenceRpm:"rpm",cadenceSpm:"spm",speedKmh:"km/h",distanceM:"m",elapsedTimeS:"s"};
      const summary=last?Object.entries(units).filter(([key])=>last[key]!==undefined)
        .map(([key,unit])=>Number(last[key].toFixed(2))+" "+unit).join(" · "):"无合法样本";
      result.push({uuid,count:samples.length,errors,unparsed,last,summary:unparsed?"未解析原始协议样本":summary||"原始协议样本",
        medianIntervalMs:intervals.length?intervals[Math.floor(intervals.length/2)]:null,
        maxIntervalMs:intervals.length?intervals[intervals.length-1]:null});
    }
    return result;
  }
  function analyze(board,reference,context){
    board=validateSnapshot(board);reference=validateSnapshot(reference);context=validateContext(context);
    const findings=[],differences=[];
    const add=(kind,code,title,evidence,action,side="board")=>findings.push({kind,code,title,evidence,action,side});
    if(!context.referenceWorks)add("unknown","reference-unverified","参考设备尚未验证",
      "未确认参考设备在同一目标、版本和角色下连接正常。","先验证参考设备，再确认参考条件。","reference");
    for(const [side,snapshot] of [["board",board],["reference",reference]]){
      if(snapshot.coverage.enumeration!=="complete")add("unknown","coverage-"+side,"服务采集范围不完整",
        snapshot.coverage.scope+" / "+snapshot.coverage.enumeration,"重新授权并采集，不能把未观察到的服务判为缺失。",side);
      for(const service of snapshot.services){
        const short=shortUuid(service.uuid);
        for(const [char,property] of SERVICE_RULES[short]||[]){
          const item=service.characteristics.find(ch=>shortUuid(ch.uuid)===char);
          if(!item){
            add(service.enumeration==="complete"?"standard":"unknown","missing-"+char,
              (LABELS[char]||char)+" 缺少必需特征 "+char,
              label(service.uuid)+" 特征枚举："+service.enumeration,
              "完整枚举后确认缺口；需固件补齐标准特征，身份复制不能修复。",side);
            continue;
          }
          const properties=item.properties.split("|");
          if(!properties.includes(property))add("standard","property-"+char,label(item.uuid)+" 属性不符合要求",
            item.properties||"无属性","服务要求 "+property+"，请校正 GATT 属性。",side);
          if(["2a63","2a5b","2a37","2a53"].includes(char)&&properties.includes("read"))add("standard","extra-read-"+char,
            label(item.uuid)+" 额外声明 Read","Measurement 应使用 Notify","修正固件属性并清理客户端旧 GATT 缓存后重测。",side);
          if(FEATURE_LENGTHS[char]){
            if(item.readStatus!=="ok")add("unknown","value-"+char,label(item.uuid)+" 尚未读取",
              item.readStatus+" "+item.readError,"重新读取能力值，读取失败不能视为能力位为零。",side);
            else if(hexBytes(item.raw).length!==FEATURE_LENGTHS[char])add("standard","feature-length-"+char,
              label(item.uuid)+" 长度不正确",item.raw,"期望 "+FEATURE_LENGTHS[char]+" 字节小端值。",side);
          }
        }
        if(short==="1826"){
          const chars=service.characteristics;
          const feature=chars.find(ch=>shortUuid(ch.uuid)==="2acc");
          if(feature&&feature.readStatus==="ok"&&hexBytes(feature.raw).length===8){
            const bytes=hexBytes(feature.raw),target=new DataView(bytes.buffer).getUint32(4,true);
            if(target&&!chars.some(ch=>shortUuid(ch.uuid)==="2ad9"))
              add(service.enumeration==="complete"?"standard":"unknown","missing-2ad9",
                "声明目标控制但未观察到 Control Point",feature.raw,
                "完整枚举后补齐 Write + Indicate 控制点。",side);
            for(const [bit,char] of [[0,"2ad4"],[1,"2ad5"],[2,"2ad6"],[3,"2ad8"],[4,"2ad7"]]){
              if(!(target&(1<<bit)))continue;
              const range=chars.find(ch=>shortUuid(ch.uuid)===char);
              if(!range)add(service.enumeration==="complete"?"standard":"unknown","missing-"+char,
                "声明目标控制但缺少 "+(LABELS[char]||char),feature.raw,
                "补齐对应只读范围特征，并与控制点值域保持一致。",side);
              else if(!range.properties.split("|").includes("read"))add("standard","property-"+char,
                "范围特征缺少 Read",range.properties,"范围特征必须可读取。",side);
            }
          }
          if(chars.some(ch=>shortUuid(ch.uuid)==="2ad9")&&!chars.some(ch=>shortUuid(ch.uuid)==="2ada"))
            add(service.enumeration==="complete"?"standard":"unknown","missing-2ada",
              "Control Point 未观察到配套 Fitness Machine Status","特征枚举："+service.enumeration,
              "完整枚举后补齐 Notify 状态特征。",side);
          for(const ch of chars){
            const char=shortUuid(ch.uuid),properties=ch.properties.split("|");
            if(["2ad4","2ad5","2ad6","2ad7","2ad8"].includes(char)){
              if(ch.readStatus!=="ok")add("unknown","value-"+char,"范围值尚未读取",
                ch.readStatus+" "+ch.readError,"重新读取范围，读取失败不能视为值域正确。",side);
              else{
                const raw=hexBytes(ch.raw),length=char==="2ad7"?3:6;
                let valid=raw.length===length;
                if(valid){
                  const view=new DataView(raw.buffer),signed=["2ad5","2ad6","2ad8"].includes(char);
                  const min=length===3?raw[0]:signed?view.getInt16(0,true):view.getUint16(0,true);
                  const max=length===3?raw[1]:signed?view.getInt16(2,true):view.getUint16(2,true);
                  valid=min<=max;
                }
                if(!valid)add("standard","range-value-"+char,"范围值长度或上下界不正确",
                  ch.raw,"应为 "+length+" 字节，最小值不得大于最大值。",side);
              }
            }
            if(["2ad2","2acd","2ada"].includes(char)&&
                (properties.includes("read")||!properties.includes("notify")))add("standard","property-"+char,
              label(ch.uuid)+" 属性不符合要求",ch.properties,"数据和状态使用 Notify。",side);
            if(char==="2ad9"&&(!properties.includes("write")||!properties.includes("indicate")||
                properties.includes("write-wr")))add("standard","property-"+char,
              "控制点属性不符合要求",ch.properties,"使用 Write + Indicate，先订阅再申请控制权。",side);
          }
        }
      }
    }
    for(const service of reference.services){
      const matching=board.services.find(s=>s.uuid===service.uuid),short=shortUuid(service.uuid);
      if(!matching){
        const observed=board.coverage.scope==="authorized"&&board.coverage.enumeration==="complete"
          &&board.coverage.allowedServices.includes(service.uuid);
        add(observed?"candidate":"unknown","service-"+short,"参考服务 "+label(service.uuid)+" 未观察到",
          observed?"本次授权范围内未返回该服务":"服务授权或采集范围无法确认",
          observed?"检查运动模式、广播开关和目标设备角色；该差异需要实测。":"补充 UUID 授权或导入原生采集后再判断。");
      }
      const uuids=new Set([...service.characteristics.map(c=>c.uuid),...(matching?matching.characteristics.map(c=>c.uuid):[])]);
      for(const uuid of uuids){
        const left=matching&&matching.characteristics.find(c=>c.uuid===uuid);
        const right=service.characteristics.find(c=>c.uuid===uuid);
        const describe=ch=>ch?ch.properties+" · "+(FEATURE_LENGTHS[shortUuid(uuid)]?featureSummary(uuid,ch.raw)
          :(ch.readStatus==="ok"?ch.raw:ch.readStatus)):"未观察到";
        if(describe(left)!==describe(right))differences.push({label:label(service.uuid)+" / "+label(uuid),
          board:describe(left),reference:describe(right)});
      }
    }
    if(board.name!==reference.name||INFO_KEYS.some(key=>board.info[key]!==reference.info[key]))
      add("candidate","identity","设备名称或 DIS 身份不同",
        "名称／厂商／型号等字符串存在差异，未证明目标按身份筛选。",
        "可用现有身份模板单独试验；它不会复制广播厂商数据、GATT 或私有握手。");
    if(context.stage==="control")add("candidate","control-handshake","控制失败需要请求与响应证据",
      "当前快照不能观察目标软件自身的握手。","核对 FTMS 能力位和目标请求；导入原生 HCI 或固件日志，不伪造支持的控制能力。");
    if(context.stage==="discover")add("unknown","advertising","原始广播尚未采集",
      "Web Bluetooth 选择器与授权服务不能还原完整广播包。",
      "确认目标搜索角色、服务开关和设备未被占用；需要时补充原生广播扫描。");
    const signals={board:signalSummary(board,findings,"board"),reference:signalSummary(reference,findings,"reference")};
    if(!board.samples.length)add("unknown","no-notifications","开发板没有通知样本","固定快照未包含通知。",
      "连接后等待实际通知，再重新冻结快照。");
    add("candidate","connection-ownership","目标端测试前释放网页连接",
      "当前开发板仅支持一个下游连接；Pages 占用时可能无法再次连接。",
      "使用「释放连接并开始实测」，暂停本站所有标签页的自动连接；其他 App 需自行断开。");
    return {verdict:"待目标端验证",findings,differences: differences.slice(0,192),signals};
  }
  return {SCHEMA,MAX_FILE_BYTES,MAX_SAMPLES,normalizeUuid,shortUuid,label,validateSnapshot,
    validateContext,capture,decodePacket,featureSummary,analyze,makeReport,importReport};
});
