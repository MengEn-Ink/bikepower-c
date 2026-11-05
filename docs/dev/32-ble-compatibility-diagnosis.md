# BLE 兼容性诊断与实施计划

用户已确认按标准缺陷修复、Pages 建设的顺序实施。目标软件和码表不设品牌白名单；兼容结论绑定目标版本、系统、设备角色与固件。

## 行为与边界

- 补齐 CPS Feature（`2A65`）、CPS Sensor Location（`2A5D`）、CSC Feature（`2A5C`）。CPS 只声明曲柄转数，CSC 只声明曲柄转数；只读特征使用固定小缓冲。CPS/CSC Measurement 使用规范要求的 Notify 属性。
- Pages 保留身份模板，增加独立兼容性诊断区。以固定快照分析服务、特征属性、Feature 原始值和通知样本；采集不完整、读取失败、无授权均不能被当作设备缺失。
- 目标软件、系统、版本可自由填写，角色覆盖功率计、踏频、心率、骑行台和跑步传感器。选择器包含 RSC；已知私有 UUID 可以显式追加授权。
- 报告区分标准问题、候选原因、未采集和人工实测结果。浏览器不提供硬件 MAC、完整原始广播或其他 App 握手；支持原始报告 JSON 导入导出，但不把导入数据当作自动实测证据。
- 进入目标端测试前保存快照、释放全部本页 GATT 会话，以同源存储与跨标签消息暂停自动连接；在途连接完成后也必须释放。明确结束测试后才恢复网页连接能力。
- 实测记录绑定固定的开发板和参考快照，包含发现、连接、数据、重连、可选控制和备注。修改场景或快照后旧结果不复用。参考设备正常连接声明需要用户明确填写。
- 身份复制仅支持现有名称/DIS 字段。标准结构和私有协议差异只能形成修复建议，不伪造支持的 Feature 位、不自动向参考设备写控制命令。

## 文件职责

| 文件 | 职责 |
|---|---|
| `main/bikepower_config.h` | 新增标准 UUID |
| `main/ble_bikepower.c` | 固定 Feature、读取回调和 GATT 表 |
| `tools/web/ble-compatibility.js` | 纯快照校验、差异分析、规则与报告模型 |
| `tools/web/ble-compatibility-ui.js` | 诊断表单、采集、报告、人工结果与连接交接 |
| `tools/web/ble-compatibility.css` | 复用现有令牌的双主题布局 |
| `tools/web/ble-monitor.html` | 采集状态与原始通知、选择器、连接暂停集成 |
| `tests/test_ble_cycling_services.py` | 生产 GATT 表和回调的 sanitizer 回归 |
| `tests/test_ble_compatibility.cjs` | 规则、权限边界、输入校验与报告回归 |
| `tests/test_ble_compatibility_browser.cjs` | 生产页面、导入导出、交接与跨标签回归 |
| `tests/ble_selftest.py` | 真机必需特征与 Feature 值验收 |

## 实施步骤

- [x] 先增加生产 GATT 回归，确认当前实现因必需特征缺失失败。
- [x] 添加 Feature 和 UUID，当前使用 `08001000` / `0200`，校验读取字节与 Notify 属性；后续标准收敛与测试见 [补测记录](33-ble-standard-test-plan.md)。
- [x] 同步真机检查脚本、BLE 技术规格和 Wiki。
- [x] 先增加诊断规则测试：完整与缺失服务、部分枚举、读失败、畸形通知、身份差异、报告往返。
- [x] 实现纯浏览器模型，固定快照与输入上限，避免扩大 HTML 单文件职责。
- [x] 增加生产页面回归：目标自由填写、读失败保留、冻结与刷新、报告导入失败、释放所有连接、在途连接和其他标签页暂停。
- [x] 实现诊断区并更新手册。将 RSC 纳入运动设备选择器，允许显式追加私有 UUID。
- [x] 接入总质量门禁，执行 host、协议、Pages 功能和浏览器双主题回归、双板编译。
- [x] 记录实际验证结果及未执行的真机／目标端验收，检查文档链接和工作树范围。

验证入口：

```sh
python3 tests/test_ble_cycling_services.py
node --test tests/test_ble_compatibility.cjs
node tests/test_ble_compatibility_browser.cjs
make -C test run
python3 tests/test_ble_protocol_vectors.py
python3 scripts/tools/quality_gate.py --no-build
```

真机仍使用项目完整流程，要求实际发现与读取新特征；没有串口或目标设备时记录 `NOT_RUN`。本任务不提升兼容性承诺，也不以离线测试替代目标端实测。

## 实际实现与验证

从干净的 `main` / `67876e5` 开始，本轮修改保留在本地工作区。Pages 复用原主题和身份模板，新增三个小模块；站点生成器原有整目录复制自动包含新模块，不需要独立发布脚本。

| 检查 | 结果 |
|---|---|
| 生产骑行 GATT 与回调 | ASan/UBSan 通过；先 RED 缺 `cps_feature_cb`，补齐后 GREEN |
| 固定快照 / 规则 / 报告 | 14 PASS / 0 FAIL，包含历史授权范围未知及导入人工证据身份 |
| 生产 Pages 诊断 | 快照独立、失败导入保持旧报告、读取失败/权限未知、注入防护、全部连接及在途 GATT、跨标签/刷新暂停、结果失效通过 |
| 双主题与手机 | 1440px 两主题输入及占位文案对比度 ≥4.5；截图视觉检查通过，390px 保留手机手册并包含诊断指导 |
| 原 Pages 会话 / 布局 | 36 PASS / 0 FAIL；真实 ESP Web Tools 错误流程与浅/深主题 360/390/768/900/1280/1600px 全部通过 |
| Host / BLE 协议 | `make -C test run`：6115/0，桥接及主备测试通过；golden vectors 三项通过 |
| 双板独立构建 | generic-c3 / m5stamp-c3，ESP-IDF v5.5.4，`PORTAL_TEST_AUTO_CONFIG=OFF`；均 app `0x113b60`，factory 剩余 `0x2cc4a0` / 72% |
| 真实生成预览 | `http://127.0.0.1:8768/#/compare`，模块加载、真实表单角色/阶段操作、未采集提示及无横向溢出通过 |
| 完整质量门禁 | `--no-build` 最终 113 PASS / 2 WARN / 0 FAIL；两条 WARN 为未提交工作区及最近提交 whitespace 检查跳过，当前 `git diff --check` 通过 |
| 文档同步 | 58 份 Markdown 链接零错误，源码事实与手机离线手册一致 |
| 实物验收 | `NOT_RUN`，没有 ESP32 USB 串口或目标端设备 |

总门禁首轮暴露浏览器测试在 `file.text()` 完成前直接断言状态的时序问题，现按导入完成状态等待，不用固定延时。另增 RED 用例确认历史 `getDevices()` 授权范围未知时不应把服务判为缺失，修正后保持未知。两项均已单独复验。

最终质量门禁日志保留 `/tmp/bp-compat-quality-final.log`。预览使用真实站点生成入口，桌面实际填写目标、选择跑步角色与无数据阶段后正确提示先采集快照；页面无横向溢出，控制台无异常。功能回归只替换蓝牙传输，不冒充实际目标端连接通过。

双板使用忽略目录 `build/compat-generic-c3` 与 `build/compat-m5stamp-c3`，分别以 `SDKCONFIG_DEFAULTS=sdkconfig.defaults;build/compat-<板型>.defaults` 覆盖板型，未改根 sdkconfig。编译日志保留 `/tmp/bp-compat-generic-build.log` 与 `/tmp/bp-compat-m5-build.log`；仅用于构建验证，未生成分发包或刷写。客户端旧 GATT 缓存需要删除旧配对重新发现。

## 目标端验收

在对应固件、软件版本和平台下分别验证功率、踏频、心率、骑行台、跑步角色；具体目标不设品牌白名单。采集正常参考设备与开发板，修复已确认的标准差异后重新冻结快照；进入实测前释放网页连接，再连续连接五次、稳定收数至少 30 分钟并检查断开重连与可选控制。把人工结果和原始日志随 JSON 保留。

完整广播/厂商数据、私有协议与目标软件控制请求仍需原生扫描、HCI 或固件日志。固定快照不证明骑鬥或任意码表已经连接成功；本记录形成时没有真实目标端通过记录，代码尚未提交。后续提交状态见 [复核记录](34-ble-compatibility-recheck.md)，实物验收仍为 NOT_RUN，未发 Release 或打 tag。
