/* 只读遥测协议；所有百分比来自设备字节，缺失能力不填示例值。 */
(function(root){
  "use strict";
  const uuid=id=>"7a5a"+(0x1000+id).toString(16)+"-4c7a-4f2a-9b3e-6d1f4a8c0b21";
  function validate(v){
    if(!(v instanceof DataView)||v.byteLength!==20)throw new Error("遥测长度应为 20 字节");
    if(v.getUint8(0)!==1)throw new Error("不支持的遥测协议版本");
  }
  function decodeMeta(v){
    validate(v);
    const period=v.getUint16(2,true),capabilities=v.getUint8(1);
    if(period<100||period>60000||capabilities!==15)throw new Error("不支持的遥测能力或周期");
    return {version:1,capabilities,period,boot:v.getUint32(4,true),
      firmware:[8,9,10].map(i=>v.getUint8(i)).join("."),
      board:v.getUint8(11)===1?"m5stamp-c3":"generic-c3",reset:v.getUint8(12),
      git:Array.from({length:7},(_,i)=>String.fromCharCode(v.getUint8(13+i))).join("").replace(/\0/g,"")};
  }
  function decode(group,v){
    validate(v);
    const flags=v.getUint8(1),sequence=v.getUint16(2,true),u32=i=>v.getUint32(i,true),u16=i=>v.getUint16(i,true);
    const out={sequence};
    if(group===0){
      const free=u32(4),minimum=u32(8),largest=u32(12),total=u32(16);
      if(!(flags&1)||!total||free>total||minimum>free||largest>free)throw new Error("无效堆快照");
      Object.assign(out,{free,minimum,largest,total,freePercent:100*free/total,largestPercent:100*largest/total});
    }else if(group===1){
      const valid=!!(flags&1);
      if(valid&&(u16(8)>10000||u16(12)>u16(8)))throw new Error("无效 CPU 百分比");
      Object.assign(out,{uptime:u32(4),cpu:valid?u16(8)/100:null,mhz:u16(10),
        mainCpu:valid?u16(12)/100:null,mainStack:u32(14),state:v.getUint8(18),profile:v.getUint8(19)});
    }else if(group===2){
      Object.assign(out,{connected:!!(flags&1),rssi:flags&2?v.getInt8(4):null,mtu:u16(6),
        interval:u16(8)*1.25,latency:u16(10),timeout:u16(12)*10});
    }else if(group===3){
      Object.assign(out,{submitted:u32(4),failed:u32(8),allocationFailed:u32(12),lockSkipped:u32(16)});
    }else throw new Error("未知遥测组");
    return out;
  }
  class Model{
    constructor(){this.meta=null;this.history=[];this.pending=new Map();this.lastSequence=null;}
    setMeta(meta){
      if(!this.meta||meta.boot!==this.meta.boot){this.history=[];this.lastSequence=null;}
      this.meta=meta;this.pending.clear();
    }
    resetWindow(){this.pending.clear();this.lastSequence=null;}
    accept(group,value,at=Date.now()){
      const data=decode(group,value),seq=data.sequence;
      const delta=this.lastSequence===null?1:(seq-this.lastSequence)&65535;
      if(delta===0||delta>=32768)return null;
      let parts=this.pending.get(seq);
      if(!parts){parts=new Map();this.pending.set(seq,parts);}
      parts.set(group,data);
      while(this.pending.size>8)this.pending.delete(this.pending.keys().next().value);
      if(parts.size!==4)return null;
      const sample=Object.assign({at,boot:this.meta?.boot},...Array.from({length:4},(_,i)=>parts.get(i)));
      this.pending.delete(seq);this.lastSequence=seq;
      this.history.push(sample);if(this.history.length>301)this.history.shift();
      return sample;
    }
  }
  class Session{
    constructor(onChange){
      this.model=new Model();this.onChange=onChange;this.state="disconnected";
      this.entries=[];this.epoch=0;this.error="";this.server=null;
    }
    async start(server){
      const epoch=++this.epoch;
      await this.clear();
      if(epoch!==this.epoch)return false;
      this.server=server;this.state="loading";this.error="";this.onChange(this);
      const current=()=>{if(epoch!==this.epoch)throw new Error("遥测会话已结束");};
      try{
        const service=await server.getPrimaryService(uuid(0));current();
        const meta=await service.getCharacteristic(uuid(1));current();
        const value=await meta.readValue();current();
        this.model.setMeta(decodeMeta(value));
        for(let i=0;i<4;i++){
          const ch=await service.getCharacteristic(uuid(i+2));current();
          const handler=e=>{
            if(epoch!==this.epoch)return;
            try{if(this.model.accept(i,e.target.value)){this.state="live";this.error="";this.onChange(this);}}
            catch(err){this.error=err.message;this.onChange(this);}
          };
          const entry={ch,handler,started:false};this.entries.push(entry);
          ch.addEventListener("characteristicvaluechanged",handler);
          await ch.startNotifications();
          entry.started=true;
          if(epoch!==this.epoch){
            ch.removeEventListener("characteristicvaluechanged",handler);
            try{await ch.stopNotifications();}catch(e){}
            current();
          }
        }
        this.state="waiting";this.onChange(this);
        return true;
      }catch(err){
        if(epoch!==this.epoch)return false;
        await this.clear();
        if(epoch!==this.epoch)return false;
        this.state=err.name==="NotFoundError"?"unsupported":"error";
        this.error=err.message;this.onChange(this);
        if(this.state==="unsupported")return false;
        throw err;
      }
    }
    async stop(){
      ++this.epoch;
      this.state="disconnected";this.server=null;this.onChange(this);
      await this.clear();
    }
    async clear(){
      const entries=this.entries.splice(0);
      for(const {ch,handler} of entries)ch.removeEventListener("characteristicvaluechanged",handler);
      for(const {ch,started} of entries)if(started){try{await ch.stopNotifications();}catch(e){}}
    }
  }
  const api={uuid,decode,decodeMeta,Model,Session};
  if(typeof module!=="undefined")module.exports=api;
  root.BikePowerTelemetry=api;
})(typeof window!=="undefined"?window:globalThis);
