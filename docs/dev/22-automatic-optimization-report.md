# 自动优化验证报告

验证基线：`cf1b462`，v4.2.1，构建及设备验收时工作区未提交。代码已于 2026-11-05 分批提交推送；原镜像证据身份保持不变，仍须用户手动验证并明确批准后发布。

## 实施范围

- 工具：分层质量门禁、源码事实摘要、Markdown 相对链接及锚点验证。
- BLE：六组配置服务拆分、通知整体快照、生命周期原子、GATT 缓冲锁内复制。
- 应用/门户：mailbox 契约、HTTP 独占共享 JSON 区、容量错误处理、超时/关闭/done/restart 同步。
- 文档：当前 300 秒规则、短固件名、模块/接口说明及已完成计划归档。
- 冗余源码：删除已从 CMake 移除且无调用方的旧 `device_template_store.c/.h`；
  历史 `ble_tpl` 数据不主动擦除，当前固件不访问该 namespace。

## 问题与修复

| 级别 | 问题 | 修复与验证 |
|---|---|---|
| P1 | Linux C11 harness 未声明 POSIX `strnlen`，CI 编译失败 | 四个编译入口补齐宏；GCC 严格告警、实际 ASan/UBSan |
| P2 | BLE 配置事务与运动服务混在大模块 | 独立配置服务；六组生产回调、双板构建及 NVS 真机 |
| P2 | 多上下文状态依赖 volatile，通知输入与 GATT 读取边界不明确 | 原子/短锁/单主任务契约；同轮快照、控制点及重连验证 |
| P2 | JSON 容量与关闭状态分散 | 命名容量、共享输出区与 done 代次隔离；实际 cJSON 边界回归 |
| P3 | 当前文档与历史实现混用、链接锚点无校验、残留未注册模板文件 | 摘要生成、归档、锚点门禁、删除无调用方源码；文档与构建复核 |

## 验证状态

| 验证 | 结果 | 证据 |
|---|---|---|
| host 与桥接领域 | 6146/0，桥接 suite 通过 | `build/automatic-operation-full.md` |
| 生产 GATT/通知快照/门户 cJSON | ASan/UBSan 通过；含提交拒收、最长转义、超限、重入和代次 | 对应 `tests/test_*runtime.py` 与 `test_ble_config_service.py` |
| 完整质量门禁 | 107 PASS / 2 预期 dirty WARN / 0 FAIL，包含 ESP-IDF 构建 | `build/automatic-quality.log` |
| full 操作门禁 | 全项通过，3 分钟 17 轮核心流程耐久 | `build/automatic-operation-full.md` |
| 文档门禁 | 78 PASS / 2 dirty WARN / 0 FAIL；最终 49 文档链接/锚点零错误 | `build/automatic-docs.log` |
| 双板独立正式配置构建 | generic-c3 / m5stamp-c3 均通过，自动配网测试开关 OFF | `build/automatic-*-build.log` |
| 资源 | app `0x10e2c0`，factory 剩余约 73%；内联 72098 字节（70.4 KiB） | 双板构建日志 |
| generic-c3 真机 | 启动、cycling/running、六组配置/NVS 与 GATT 重建、双次重连、307 秒耐久全部通过；258 条 CPS，扫描/连接/空通知失败均 0，原配置恢复 | `build/automatic-evidence/operation-device.md` |

门禁在 2026-11-04 夜间至 11-05 凌晨完成；各日志保留实际运行时间。
最终补充的 Markdown Setext/代码内 ID 边界与最长配置响应已单独复跑通过。
清理旧模板源文件后双板复编仍通过，并逐区段确认与实际验收镜像完全一致。
最终专项门禁为 106 PASS / 2 dirty WARN / 0 FAIL，构建明确跳过，双板构建另有证据。

## 镜像身份

本轮镜像位于 `build/automatic-evidence/`，与旧 `build/release/` 候选隔离。
基础提交 `cf1b462ca997d0b8ac479fcd4cabd115013a09d5`，`workspace_dirty=true`，
不是 clean HEAD 发布构建。`image-identity.json` 保存构建时源码文件 SHA256；
后续删除的两个旧模板存储文件未参与本轮编译，不改变镜像区段。

独立设备证据 `device-acceptance-generic-c3.json` 最终绑定为 `cf1b462-dirty`，
附实际镜像 SHA256，防止误用为 clean HEAD 的正式发布证据。
已验证全部 P0 自动项为 PASS，同时 clean HEAD Git ID 校验会拒绝此证据。

| 镜像 | 合并文件 SHA256 |
|---|---|
| `generic-c3-v4.2.1.bin` | `5f7dc7773a0b2ca2f4809171fa1d635777cc7b5a4bd86e011987c1d96ecd9b24` |
| `m5stamp-c3-v4.2.1.bin` | `fbd874eac2a94cdd1d0cef321740c0e1b6141506d194649655d6ebea238dd33f` |

烧录使用同一构建的 bootloader/partition/app 区段保留 NVS；分发身份以合并 bin
哈希记录。无固件业务参数或 schema 变化，删除未使用的旧 `WIFI_RENEW_MS`，
实际续期仍引用 `WIFI_SHUTDOWN_MS=300000`。

## 验收边界

真实功率计/下游 App/WiFi 三方共存、M5 实物、Safari/手机配网、物理按钮/LED、
Web Bluetooth/Web Serial 原生授权、标准骑行 App 和可选 72h 老化保持待办。
配网态/三方共存最低堆水位仍需实际热点验证；当前 WARN 日志与离线 cJSON
替身不证明设备在共存峰值下满足 80 KiB 堆水位。
旧候选包与旧证据保留，不能作为本轮未提交镜像的通过证据。
并行 Pages 布局任务的改动保留，本报告不将其计入本轮完成项。

## Linux CI harness 修复

远端 CI `41859263109` 的桥接 harness 编译失败，导致 Pages 部署跳过。
日志只保留 Python 编译失败 traceback；本地 GCC 14.2 按相同 C11/告警参数
复现 `device_identity.c` 的 `strnlen` 隐式声明错误。C11 模式下需显式启用 POSIX 声明。
四个宿主机 harness 编译命令补齐 `-D_POSIX_C_SOURCE=200809L`，固件源码不变。
本地 GCC 语法/严格告警检查以及 macOS 实际 ASan/UBSan 回归全部通过。
GCC 检查使用本地交叉工具链，本机结果不代表 Linux ASan 运行；后续基线 `5c97e96` 的远端 [CI 41867471119](https://github.com/MengEn-Ink/bikepower-c/actions/runs/41867471119) 已完成且成功。
修复后的完整 quick 门禁已通过，报告为 `build/automatic-operation-quick-final.md`。
生产门户 cJSON 回归放在 full 操作门禁，quick 不引入本地 ESP-IDF 源码依赖；
完整质量门禁仍执行该生产回归，不把缺依赖计为通过。

## 提交与发布

2026-11-05 按用户“先提交代码”指令分批提交并推送：

- `61991f7`：固件同步修复与生产运行时回归。
- `b0595f9`：废弃模板存储模块清理。
- `3d419a1`：Linux CI 编译修复与维护门禁。
- `5305a04`：并行 Pages 体验与独立离线手册。
- 文档与规则单独提交，涵盖本报告、历史归档和源码事实摘要。

后续基线 `5c97e96` 的工程 CI `41867471119` 与 Pages 部署 `41869049536` 均已成功；原失败运行保留历史结果。此结论不覆盖后续稳定性异常修复提交；未创建 tag 或 Release。
提交源码不改变上文实际验收镜像的 dirty 身份，不将旧证据改记为新 HEAD 通过。

## 保留与清理

本机 `build/` 约 613 MiB（包含两个独立板型验证目录），`tests/reports/` 约 11 MiB。
现有候选固件、测试证据、原型与并行 Pages 资产保留，没有确认可删的缓存目录。
源码文件清理与忽略产物清理分别记录，不以删除旧证据代替验证。
