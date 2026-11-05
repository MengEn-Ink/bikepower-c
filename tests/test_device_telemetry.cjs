/* 人工协议输入测试网页解码、窗口组合与订阅回滚。 */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const file = path.join(__dirname, "../tools/web/device-telemetry.js");
assert.ok(fs.existsSync(file), "缺少生产遥测客户端");
const {decode, decodeMeta, Model, Session, uuid} = require(file);
const record = (group, seq = 1) => {
  const b = new Uint8Array(20), v = new DataView(b.buffer);
  b[0]=1;b[1]=group===2?3:1;v.setUint16(2,seq,true);
  if(group===0){v.setUint32(4,81920,true);v.setUint32(8,40960,true);v.setUint32(12,65536,true);v.setUint32(16,327680,true);}
  if(group===1){v.setUint32(4,12,true);v.setUint16(8,2500,true);v.setUint16(10,160,true);v.setUint16(12,100,true);v.setUint32(14,2048,true);}
  if(group===2){v.setInt8(4,-65);b[5]=1;v.setUint16(6,23,true);v.setUint16(8,24,true);v.setUint16(12,400,true);}
  return v;
};
assert.equal(decode(0,record(0)).freePercent,25);
assert.equal(decode(1,record(1)).cpu,25);
assert.equal(decode(2,record(2)).rssi,-65);
assert.throws(()=>decode(0,new DataView(new ArrayBuffer(19))),/长度/);
const invalid=record(1);invalid.setUint8(0,2);assert.throws(()=>decode(1,invalid),/版本/);
const missing=record(1);missing.setUint8(1,0);assert.equal(decode(1,missing).cpu,null);
const meta=new Uint8Array([1,15,232,3,1,0,0,0,4,2,1,0,1,97,98,99,100,101,102,103]);
assert.equal(decodeMeta(new DataView(meta.buffer)).git,"abcdefg");
const model=new Model();
model.setMeta(decodeMeta(new DataView(meta.buffer)));
assert.equal(model.accept(0,record(0,65535),100),null);
assert.equal(model.accept(1,record(1,0),101),null);
assert.equal(model.accept(2,record(2,65535),102),null);
assert.equal(model.accept(3,record(3,65535),103),null);
assert.equal(model.accept(1,record(1,65535),104).sequence,65535);
for(let g=0;g<4;g++)model.accept(g,record(g,0),110);
assert.equal(model.history.length,2);
assert.equal(model.history[1].sequence,0);
for(let g=0;g<4;g++)model.accept(g,record(g,65534),120);
assert.equal(model.history.length,2);
for(let seq=1;seq<310;seq++)for(let g=0;g<4;g++)model.accept(g,record(g,seq),seq*1000);
assert.equal(model.history.length,301);
model.setMeta({...model.meta,boot:2});assert.equal(model.history.length,0);
for(let seq=1;seq<40;seq++)model.accept(0,record(0,seq),seq);
assert.ok(model.pending.size<=8);
model.resetWindow();assert.equal(model.pending.size,0);

(async()=>{
  const chars=Array.from({length:5},(_,i)=>({
    listeners:new Set(),stops:0,
    async readValue(){return i===0?new DataView(meta.buffer):record(i-1);},
    async startNotifications(){if(i===3)throw new Error("订阅失败");return this;},
    async stopNotifications(){this.stops++;},
    addEventListener(_,fn){this.listeners.add(fn);},
    removeEventListener(_,fn){this.listeners.delete(fn);}
  }));
  const service={async getCharacteristic(id){return chars[parseInt(id.slice(4,8),16)-0x1001];}};
  const session=new Session(()=>{});
  await assert.rejects(session.start({async getPrimaryService(){return service;}}),/订阅失败/);
  assert.ok(chars.every(ch=>ch.listeners.size===0));
  assert.equal(chars[1].stops,1);assert.equal(chars[2].stops,1);
  const legacy=new Session(()=>{});
  assert.equal(await legacy.start({async getPrimaryService(){const e=new Error("没有服务");e.name="NotFoundError";throw e;}}),false);
  assert.equal(legacy.state,"unsupported");
  let oldCalls=0,newCalls=0;
  const racing=new Session(()=>{});
  const old=racing.start({async getPrimaryService(){oldCalls++;throw new Error("旧连接不应继续");}});
  const fresh=racing.start({async getPrimaryService(){newCalls++;const e=new Error("没有服务");e.name="NotFoundError";throw e;}});
  await Promise.allSettled([old,fresh]);
  assert.equal(oldCalls,0,"并发启动必须取消旧连接");
  assert.equal(newCalls,1);
  assert.equal(uuid(0),"7a5a1000-4c7a-4f2a-9b3e-6d1f4a8c0b21");
  console.log("PASS: 严格解码、同序组合、回绕、历史容量、boot切分、订阅回滚和旧固件");
})().catch(e=>{console.error(e);process.exitCode=1;});
