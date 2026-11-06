# BLE 标准一致性补测与执行记录

## 范围与步骤

用户已确认“按建议补全测试”，本轮执行标准一致性修复与验证。测试阶段保留工作区已有兼容诊断改动，不提交、不发布；后续用户授权的提交状态见 [复核记录](34-ble-compatibility-recheck.md)。

1. Step1 测试准备：完成；临时目录由 prepare_test.sh 创建。
2. Step2 上下文：完成；LANG=cpp，EXEC_SOURCE 为空。AGENTS 规定 C 编译、领域 host 与设备验收；沿用生产函数提取、C11、ASan/UBSan 与 Python 调度。
3. Step3 范围：完成；主目标为 `main/ble_bikepower.c` 的 Feature 回调、双 profile GATT 表、`start_advertising`、`ftms_cp_cb`、响应发送与 GAP 生命周期。CPS 新回调来源为未提交 diff；其他目标由同一协议链路的调用关系纳入。Pages 解析和设备自测为配套回归。
4. Step4 缺陷分析：完成，见下表。
5. Step5 生成→验证→修复：完成；正确行为断言全部通过，没有放宽断言或删除失败场景。
6. Step6 覆盖率：按技能条件跳过；AGENTS/用户未要求覆盖率阈值，EXEC_SOURCE 为空，不报告估算覆盖率。
7. Step7 结果报告：完成；执行 utree flush，退出码 0，证据见下文。

## 缺陷与测试映射

| 编号 | 优先级 | 责任目标 | 触发与当前错误 | 正确期望 |
|---|---|---|---|---|
| B1 | P1 | CPS Feature 回调 | 读取 2A65 返回未定义分布式标志 00 | 曲柄、非分布式总功率：08001000 |
| B2 | P1 | FTMS Feature/GATT | 跑步声明错位；声明目标控制但缺范围；属性多出 Read/WriteNoRsp | 0414000001000000；2AD4/2AD8 小端范围；标准属性 |
| B3 | P1 | start_advertising | 不完整 UUID 标为完整，FTMS 无类型字段 | UUID 部分列表；FTMS Service Data；所有掩码/长 UTF-8 名均 ≤31 字节 |
| B4 | P1 | ftms_cp_cb/GAP | 未订阅/未获权即可控制；未等待 indication 确认即可重入 | CCCD/控制权/串行门禁；断开与复位清状态 |
| B5 | P1 | ftms_cp_cb/响应发送 | 空包被当成功；额外字节被接受；发送错误吞掉 | 长度严格校验；可观察发送失败并清过程状态 |
| B6 | P1 | FTMS Status | 旧规则把启动误标为 0x03，停止未带控制信息 | Started=0x04；Stop/Pause 通知第二字节为 1/2 |

缺陷分类为标准契约与错误处理，均有生产代码责任位置及确定输入。测试断言修复后的行为，先执行 RED，再修改生产。单测技能默认仅修改测试；本轮生产修复来自用户对前述建议的明确授权。

## 实施选择

沿用现有扁平 BLE 模块，静态范围使用领域常量，控制点只投递应用回调。主广播增加 7 字节 FTMS Service Data 后，名字预算从 18 降为 11 字节，完整名仍放扫描响应；截断不得切断 UTF-8。仅 FTMS 存在时发布类型字段。

不新增角色配置或模拟电量。FTMS 控制点的 Encryption 规范要求单独记录：当前项目无配对鉴权策略，本轮不擅自引入配对流程，不能宣称完整认证合规。

## 验证计划

先执行生产 GATT/Feature、广播与控制点测试，随后运行 host、协议 golden vectors、Pages 回归、门户保存、核心耐久与总门禁；最后双板编译和串口扫描。无法执行的真机测试必须明确标为 NOT_RUN。

## 实际验证结果

| 验证 | 结果与证据 |
|---|---|
| 修复前 CPS | `python3 tests/test_ble_cycling_services.py` 退出码 1；生产读取字节与正确期望不一致 |
| 修复前范围/广播/控制 | Feature 缺范围；广播完整标志断言失败；修复夹具依赖后控制测试在未订阅门禁断言失败 |
| 生产 GATT 与标准回调 | `test_ble_cycling_services.py`、`test_ble_standard_contract.py` 全通过；后者包含 Feature、control、advertising、lifecycle、transmission 五组 |
| 测试基础设施收敛 | 初轮缺 ESP-IDF 头、日志空宏未使用告警、名称夹具越长和提取器误匹配函数调用；均仅修夹具。旧静态契约因新增参数校验 switch 匹配错误，改为提取最后的执行分支，保留行为断言 |
| Host | `make -C test run`：6115/0；功率桥接及三设备状态机通过 |
| Pages | `test_ble_compatibility.py`：15/15 规则测试及生产页面交接/双主题回归通过；新规则检查 FTMS 目标范围及属性 |
| 总质量门禁 | `python3 scripts/tools/quality_gate.py --no-build`：114 PASS / 2 未提交工作区 WARN / 0 FAIL；包含协议 vectors、重连/profile harness、门户/Pages、文档链接及离线手册 |
| 核心耐久 | `test_core_flow_endurance.py`：180.0 秒、15 轮通过；`build/standard-core-flow-endurance.md` |
| 操作门禁 | `operation_gate.py --level quick` 全通过；`build/standard-operation-quick.md` |
| 双板正式配置 | 隔离 `build/compat-generic-c3`、`build/compat-m5stamp-c3`，ESP-IDF v5.5.4 编译通过，factory 分区余量 72%；`build/standard-generic-build.log`、`build/standard-m5stamp-build.log` |
| 真机扫描 | 仅 Bluetooth/debug-console/wlan-debug 串口，无 ESP32 USB 串口；烧录、实际广播/读取/控制、真实无线耐久均 NOT_RUN |

新测试断言直接运行生产函数，不模拟正确业务结果。C golden bytes 手工维护；广播包含双模式 × 7 掩码 × 短名/23 字节 ASCII/中文名共 42 个组合。控制涵盖所有现有操作、两端边界、错误长度、未获权、重入、应用拒绝、响应发送失败。GAP 验证发送成功尚未确认时维持 pending，确认/错误后释放，并隔离其他句柄。Indication 测试验证零等待锁、分配与协议栈返回码、断开/停止守卫。

## 剩余验收边界

- 当前板卡与骑鬥/码表上的发现、连续五次连接、重连和 30 分钟数据稳定尚未执行，不宣称任意 BLE 终端已兼容。
- FTMS Table 4.1 Encryption 尚未实现；配对、缓存与手机权限策略需独立设计和真机验证。
- 应用已接收命令后若 indication 发送失败，事务/控制权会释放，但已投递的命令不回滚；ATT 返回资源错误不等于应用没有执行。
- 本记录之后的专项复核已修正来源切换 Feature、桥接心率声明、跨特征 indication 及部分跑步掩码；新证据见 [兼容复核](34-ble-compatibility-recheck.md)。可选目标变更 Status 等进一步规范收敛仍需验证。
- 本记录形成时固件仅本地编译；后续源码推送见复核记录。未打 tag 或发布固件，无当前候选设备验收证据。

## 规范来源

- [CPS v1.1](https://www.bluetooth.com/specifications/specs/html/?src=CPS_v1.1%2Fout%2Fen%2Findex-en.html)：Feature 的曲柄与分布式系统字段。
- [FTMS v1.0.1](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/ftms-v1-0-1_1756429637/FTMS_v1.0.1/out/en/index-en.html)：§3、Table 4.1/4.3/4.4、控制过程 §4.16、Status §4.17。
- [CSS v11](https://www.bluetooth.com/specifications/specs/css-11/)：部分 UUID 列表与 Service Data AD 格式。
