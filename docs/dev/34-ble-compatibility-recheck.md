# BLE 能力与当前设备连接复核

核对日期：2026-11-05。用户要求继续检查已连接开发板，并复核与标准功率计的协议差异、稳定性和兼容性。核对时工作区保留已有未提交变更；用户随后授权“先提交合并”，源码已分批推送 main，记录见下文。本报告区分源码/本地验证与真实终端验收。

## 当前 USB 与 BLE 证据

| 检查 | 当前结果 | 可得结论 |
|---|---|---|
| `python3 -m serial.tools.list_ports -v` | 仅 Bluetooth-Incoming-Port、debug-console、wlan-debug | 当前没有可用于 ESP32 烧录的串口 |
| `ioreg -r -c IOUSBHostDevice -l -w 0` | 无匹配设备 | USB 设备枚举层尚未看到板卡或转串口芯片 |
| `ioreg -r -c IOSerialBSDClient -l -w 0` | 仅上述三个系统端口 | 不是 ESP32 端口被其他串口程序占用的证据 |
| `system_profiler SPUSBDataType SPThunderboltDataType -detailLevel mini` | 无 USB 项，三个雷雳端口无雷雳设备连接 | USB 数据链路需要实物排查；雷雳状态本身不能否定普通 USB 连接 |
| Bleak 扫描 12 秒 | 发现 124 台设备，按 BikePower 名称/配置 UUID 筛选为 0 | 未识别到板卡广播；自定义名称、已有连接或未广播仍可能造成漏检 |
| 串口守护进程 PID 文件 | 不存在 | 不使用历史串口日志充当本次连接证据 |

板卡已插入是用户提供的实物状态，本报告不据此反向断言未插入。当前未执行烧录、候选启动日志、GATT 真机读写或目标 App 实测，均为 **NOT_RUN**。

建议先用确认可传数据的 USB 线直接连接这台 Mac，检查 macOS 是否弹出配件连接许可；如是原生 USB 的 ESP32-C3，可按板卡说明尝试 BOOT 保持按下、点按 RESET，再释放 BOOT，使 ROM 下载模式重新枚举。普通运行模式未枚举可能涉及线材、转接链路、配件授权或固件 USB 状态，当前证据不能定位其中一种原因。

## 与标准功率计的差异

下表是功能与规范要求对照，不是某一台成熟设备的实际抓包对照。

| 能力 | 当前本地候选 | 必需性与兼容影响 |
|---|---|---|
| CPS Measurement `2A63` | Notify，功率/曲柄数据，1 秒节拍 | 基础功率采集路径已具备 |
| CPS Feature `2A65` | 已补齐 `08001000` | 曲柄能力与非分布式总功率声明已修正 |
| Sensor Location `2A5D` | 已补齐合法值 `05` | 表示左曲柄，不能据此宣称左右独立测量 |
| CSC Measurement/Feature | Notify + `0200` | 与实际曲柄数据一致，不宣称轮速 |
| FTMS Feature/Range | 骑行功率范围 `2AD8`；跑步速度范围 `2AD4`；修正跑步能力位 | 条件必需范围与领域输入校验已补齐 |
| 广播 UUID 与 FTMS Service Data | 部分 UUID 列表；骑行 `2618012000`、跑步 `2618010100` | 修复列表完整性语义与运动类型；名称按 UTF-8 边界控制在 31 字节广播预算内 |
| FTMS 属性与控制过程 | Write + Indicate；CCCD、控制权、过程串行、严格长度、发送失败与断连清理 | 标准一致性与异常处理已改善，仍需设备时序实测 |
| FTMS Status | Reset=`01`、Started=`04`、Stopped/Paused=`02` 加参数 | 已纠正旧状态编号与停止/暂停布局 |
| DIS 设备信息 | 厂商、型号、序列号、硬件/固件/软件版本六项 | 常规身份信息完整；复制 DIS 不会复制私有握手 |
| System ID、PnP ID、BAS 电量 | 未提供 | 常规功率采集非必需；无电量测量依据时不能编造电量 |
| CPS Control Point、Vector、左右平衡、扭矩等 | 未提供且未宣称相关能力 | 可选扩展，校准/曲柄长度/高级测量需有真实实现和数据依据 |
| GAP/GATT/CCCD/Service Changed | NimBLE 内置注册或生成 | 不能记为缺失 UUID；缓存失效的实际交互尚需验证 |
| 双下游连接 | 当前仅维护一个 peripheral 连接 | 网页连接会占用下游名额，测试骑鬥/码表前须释放网页连接 |
| 原始广播/私有握手对照 | 尚无参考设备抓包 | Web Bluetooth 快照无法替代完整 ADV/SCAN_RSP、其他 App 握手与断开原因 |

## 已有本地验证

复用本轮已完成证据，不将离线耐久称为无线耐久：

- Host：6115 断言，0 失败。
- 总门禁：114 通过，2 项未提交工作区警告，0 失败。
- 双板：ESP-IDF v5.5.4 正式配置编译通过；app `0x114050` 字节，factory 剩余 72%。
- 标准生产函数测试：Feature、控制、广播、生命周期、发送失败；广播覆盖 42 组模式/掩码/名称组合。
- Pages：15 条诊断规则与页面交接、双主题浏览器回归通过。
- 核心流程离线耐久：180 秒、15 轮通过。

证据见 [标准补测记录](33-ble-standard-test-plan.md)、[质量日志](../../build/standard-quality.log)、[通用编译](../../build/standard-generic-build.log)、[M5 编译](../../build/standard-m5stamp-build.log)。

## 验收边界与下一阶段选择

### 本次页面复核与修复

采用 bits-code-guard 分组审查、bits-unit-test-gen 与 TDD 复现。新增五项纯函数/生产连接用例最初全部失败，原有 15 项仍通过；新增跑步订阅用例修正测试提取范围后，也按预期命中未订阅缺陷。浏览器复现结束实测后刷新丢失手动连接限制。

已修复：

- 已声明目标控制但缺少 Control Point，以及 Control Point 缺少配套 Status 的条件缺口；部分枚举继续报告未知。
- 已读取范围的长度与上下界错误；读取失败继续报告未知。
- Treadmill Data 按 flags 解析与原始通知订阅；未支持协议显式报告未解析，不作为已校验零异常。
- 旧连接的完成与失败清理只作用于自身代次，不能断开新代次已经接管的 GATT。
- 实测结束后的手动连接限制跨刷新、新标签页保留；显式手动连接解除限制。

验证：`node --test tests/test_ble_compatibility.cjs` 21/21 通过；`node tests/test_ble_compatibility_browser.cjs` 通过，包含旧请求 resolve/reject、跨标签刷新、读取失败、注入防护与双主题。Step6 按条件跳过覆盖率；Step7 `utree flush` 退出 0。不将这些传输桩测试计为无线真机证据。

### 固件专项复核发现

独立复核已确认并修复 FTMS/配置服务的跨特征 indication 竞争、跑步掩码 1/2/5/6 的 RSC 通知遗漏、模拟/桥接切换时 Feature 变化，以及桥接 FTMS 心率字段未声明。新回归先观察到能力与桥接控制断言失败、重复 indication 发送断言失败；跑步旧条件在临时生产函数中也被新回归明确拒绝。

修复后的行为：

- FTMS 与配置响应共享 8 条静态 FIFO。写入控制操作前预留位置，容量不足返回 ATT 资源不足且不执行；异步 COMMIT 保留自己的 token，旧连接结果不能填到新连接。
- 队列复制响应字节，只有一个 indication 在途。NOTIFY_TX `status=0` 不释放，EDONE 才推进；错误不盲目重试下一条，而是终止连接并等待清理。停止、复位和断连清理队列。
- 跑步任一速度/踏频选项启用就发送完整 RSC，七种掩码与 GATT 注册一致。
- 骑行 Feature 固定为 `0244000008000000`，模拟/桥接切换保持稳定，补齐心率能力。真实功率桥接期间目标功率暂不可控，返回 Operation Failed，不修改模拟或持久配置。

新验证证据：

| 项目 | 结果 |
|---|---|
| 生产函数 ASan/UBSan | Feature、控制、42 组广播、生命周期、队列、7 种跑步通知组合通过 |
| 队列异常 | FTMS→配置→配置、预留未完成不得越过、缓冲复制、满队列、status=0、错误句柄/连接、EDONE、锁忙、发送/分配失败、复位旧 token、同步完成均通过 |
| 配置服务 | 容量不足的 BEGIN 不执行；原有八组、revision 与异步提交回归通过 |
| Host/协议/桥接/跑步 | Host 6115/0；协议向量、桥接会话/主备契约、跑步四阶段 harness 通过 |
| Pages | 21/21 与双主题、旧请求成功/失败、跨标签刷新浏览器回归通过 |
| 配置与文档事实 | 282 通过、0 失败 |
| 最终总质量门禁 | `--no-build`：114 PASS / 2 未提交工作区 WARN / 0 FAIL；构建由下行独立验证 |
| 最终文档门禁 | 79 PASS / 2 未提交工作区 WARN / 0 FAIL |
| 双板正式配置编译 | generic-c3、m5stamp-c3 均通过，app `0x1143b0`，factory 剩余 72% |

日志：[生产回归](../../build/recheck-standard.log)、[质量门禁](../../build/recheck-quality.log)、[文档门禁](../../build/recheck-docs.log)、[通用编译](../../build/recheck-generic-build.log)、[M5 编译](../../build/recheck-m5stamp-build.log)。剩余规范差距见 [HTML 审查报告](../../build/standard-review.html) 与 [Markdown 报告](../../build/standard-review.md)；已修复项不重复列为未解决缺陷。上述结果来自提交前验证；未打 tag 或发布固件。

### 提交合并记录

用户授权后，确认当前 main 与 origin/main 一致，直接分批提交并推送 main：

- `7b9fccf`：固件标准能力、响应预留队列与协议回归，已推送。
- `8e387fe`：Pages 兼容诊断、连接交接及回归接线，已推送。
- 文档批同步规范、手册与验证记录；最终远端 CI 和 Pages 结果需按最终提交核对，源码推送不代表固件已发布或真机通过。

独立的 `design-demos/bikepower-ui/device-status/` 原型保持未跟踪，未纳入本轮提交。

FTMS 控制点 Encryption 尚未实现，因此不能宣称完整 FTMS 规范合规。配对/绑定策略、profile 切换后的旧客户端缓存，以及参考功率计私有协议都需要独立设计或设备证据。

建议顺序：

1. 先收敛能由生产函数测试明确复现的标准/异常缺陷，维持现有配置 schema。
2. USB 恢复后，烧录已核对板型的候选，检查启动、空闲堆、双 profile、NVS 恢复、控制确认、断连重连与无线耐久。
3. 若标准终端仍拒绝连接，记录具体系统/软件版本和功率计或骑行台角色；在释放网页连接后验证五次连续连接与 30 分钟数据稳定，再按抓包证据判断是否需要独立功率计角色或目标端方案。

独立功率计角色需要配置/API/Pages/GATT 联动，覆盖面较大；品牌专用适配需要成功参考设备与失败设备的原始数据，维护成本更高。当前优先完成标准收敛和真机证据。

## 规范来源

- [CPS v1.1](https://www.bluetooth.com/specifications/specs/html/?src=CPS_v1.1%2Fout%2Fen%2Findex-en.html)
- [CPP v1.1](https://www.bluetooth.com/specifications/specs/html/?src=CPP_v1.1%2Fout%2Fen%2Findex-en.html)
- [FTMS v1.0.1](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/ftms-v1-0-1_1756429637/FTMS_v1.0.1/out/en/index-en.html)：Table 4.1 条件范围/属性/Encryption，§3 广播，§4.16 控制，§4.17 状态。
- [CSS v11](https://www.bluetooth.com/specifications/specs/css-11/)
