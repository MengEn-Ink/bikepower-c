"use strict";
window.DeviceStatusApi = {
  // 演示接入层；正式实现应从共享 GATT 连接解码遥测，不能改用云端 HTTP。
  async connectDemo(){
    await new Promise(resolve=>setTimeout(resolve,350));
    return {isDemo:true,connected:true,capabilities:["system","memory","link"]};
  },
  async readDemoSnapshot(scenario){
    return {isDemo:true,...window.DeviceStatusMock.scenarios[scenario]};
  }
};
