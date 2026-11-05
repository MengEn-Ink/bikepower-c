"use strict";
// 全部数据仅用于设计演示，不是设备测量或验收证据。
window.DeviceStatusMock = {
  device: {name:"BikePower C", uptime:3664, heapCapacity:320, interval:30, mtu:23, attempts:1800},
  scenarios: {
    normal:{heap:186.4,min:171.2,largest:128,rssi:-57,p95:1018,age:.2,failures:0,disconnects:0,sequence:1842},
    low:{heap:72.6,min:68.1,largest:31.8,rssi:-61,p95:1044,age:.3,failures:2,disconnects:0,sequence:1842},
    stale:{heap:182.8,min:171.2,largest:126,rssi:-79,p95:2480,age:8.4,failures:4,disconnects:0,sequence:1834},
    high:{heap:178.8,min:169.4,largest:119,rssi:-58,p95:1114,age:.2,failures:0,disconnects:0,sequence:1842},
    weak:{heap:184.1,min:171.2,largest:126,rssi:-84,p95:1560,age:.7,failures:5,disconnects:0,sequence:1842},
    legacy:{heap:null,min:null,largest:null,rssi:null,p95:null,age:null,failures:null,disconnects:0,sequence:null},
    disconnected:{heap:null,min:null,largest:null,rssi:null,p95:null,age:null,failures:null,disconnects:1,sequence:null}
  },
  tasks:[
    {name:"main",cpu:4.2,priority:5,stack:2864,status:"就绪"},
    {name:"nimble_host",cpu:6.8,priority:21,stack:3128,status:"阻塞"},
    {name:"esp_timer",cpu:3.1,priority:22,stack:2432,status:"阻塞"},
    {name:"button_task",cpu:.4,priority:5,stack:1808,status:"阻塞"},
    {name:"Tmr Svc",cpu:.3,priority:1,stack:1472,status:"阻塞"},
    {name:"其他任务",cpu:3.6,priority:null,stack:null,status:"汇总"},
    {name:"IDLE",cpu:81.6,priority:0,stack:864,status:"就绪"}
  ],
  events:[
    {time:"00:01:04",title:"首份遥测快照",detail:"内存、系统、链路能力可用",source:"设备"},
    {time:"00:01:03",title:"遥测订阅完成",detail:"每秒采样 · 基础包支持 MTU 23",source:"网页"},
    {time:"00:01:02",title:"网页蓝牙连接建立",detail:"沿用工具台共享连接",source:"网页"},
    {time:"00:00:00",title:"设备启动",detail:"启动标识已更新",source:"设备"}
  ],
  sample(scenario,tick){
    const base=this.scenarios[scenario];
    const sample={...base,cpu:null,uptime:this.device.uptime+tick,attempts:this.device.attempts+tick*3,tick};
    if(base.heap===null)return sample;
    if(scenario==="stale")return {...sample,cpu:18.4,uptime:this.device.uptime,attempts:this.device.attempts,age:base.age+tick};
    const wave=Math.sin(tick*.35), slow=Math.sin(tick*.11);
    sample.heap=base.heap+wave*2.4+slow*1.2;
    sample.largest=base.largest+wave*1.4;
    sample.cpu=(scenario==="high"?88:18.4)+wave*(scenario==="high"?4:3)+slow*1.2;
    sample.rssi=Math.round(base.rssi+wave*2);
    sample.p95=Math.round(base.p95+slow*12);
    sample.age=base.age;
    sample.sequence=base.sequence+tick;
    return sample;
  },
  series(scenario,metric,count){
    const sample=this.scenarios[scenario];
    return Array.from({length:count},(_,i)=>{
      const t=i/Math.max(count-1,1);
      if(metric==="memory"){
        const end=sample.heap||186.4;
        return scenario==="low"?186.4-(186.4-end)*t+Math.sin(i*.55)*2:end+Math.sin(i*.5)*2.2+Math.cos(i*.17)*1.3;
      }
      if(metric==="largest")return (sample.largest||128)+Math.sin(i*.5)*1.3;
      if(metric==="cpu")return 18.4+Math.sin(i*.4)*3+Math.cos(i*.11)*2;
      if(metric==="rssi")return (sample.rssi||-57)+Math.sin(i*.36)*2;
      return scenario==="stale"?1000+Math.pow(t,4)*1480:1000+Math.sin(i*.7)*12+Math.cos(i*.29)*6;
    });
  }
};
