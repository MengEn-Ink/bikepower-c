# v3.1.6 迭代规划：在线 OTA 下载升级 + FTMS 扩展与内存优化

> 规划日期：2026-06-27
> 最近更新：2026-06-28（在线 OTA 下载升级已实现，编译通过待真机测试）
> 当前版本：v3.1.5（OTA 上传 + Captive Portal 已完成）
> 目标版本：v3.1.6
> 状态：在线 OTA 下载升级已编码完成，待真机测试

---

## 一、迭代背景

v3.1.5 已完成 OTA 上传升级和 Captive Portal 自动弹配网页，但升级需用户手动下载 bin 文件再通过配网页上传，体验不佳。v3.1.6 聚焦三个方向：

1. **在线 OTA 下载升级（核心）**：设备连接家庭 WiFi 后自动检测最新版本并下载升级，无需手动下载 bin 文件
2. **FTMS 扩展**：提升骑行 App 兼容性，补全 FTMS 规范特征
3. **内存优化**：降低配网态内存占用，为后续功能腾出空间

### 当前基线

| 指标 | v3.1.5 实测值 |
|------|-------------|
| 启动态空闲堆 | 176 KB |
| BLE 连接态 | ~171 KB |
| 配网态（BLE+WiFi 共存） | 134 KB |
| 满负载（配网+手机连接） | 117 KB |
| WiFi 残留（配网后） | 129 KB（-48KB） |
| 固件体积 | 1017 KB（占 ota 分区 55%） |

---

## 二、在线 OTA 下载升级方案（v3.1.6 核心，已实现）

### 2.1 方案概述

设备在配网态（SoftAP 已启动）下，用户在配网页面输入家庭 WiFi 凭证，设备切换 APSTA 模式连接家庭路由器，通过 HTTPS 从 Gitee 仓库拉取 `latest.json` 检查最新版本，对比版本号后提示用户升级。升级时设备停 BLE + 停 SoftAP 独占 STA，HTTPS 下载固件 bin 写入 OTA 分区，完成后重启进入新固件。

### 2.2 固件分发源

| 方案 | 说明 | 选择 |
|------|------|------|
| Gitee Release 附件 | 仓库改公开，Release 上传 bin+json | ✅ 已选 |
| Gitee 私有 + Token | 需配置 token，URL 带认证 | ❌ 复杂 |
| 自建 HTTP 服务器 | 完全自主但需运维 | ❌ 成本高 |
| 混合方案 | Gitee 为主 + 自建镜像 | ❌ 过度设计 |

**实现**：固件 bin 和 `latest.json` 放仓库 `ota/` 目录，通过 Gitee raw URL 访问（仓库需公开）。

### 2.3 射频资源管理

| 场景 | 模式 | BLE | SoftAP | STA | 说明 |
|------|------|-----|--------|-----|------|
| 检查版本 | APSTA 共存 | 在线 | 在线 | 临时连接 | 配网页可继续访问 |
| 下载升级 | STA 独占 | 停止 | 停止 | 独占 | 释放射频确保稳定性 |

**检查版本**：APSTA 共存，BLE 和配网页保持在线，STA 临时连路由器获 IP 后拉 JSON，完成切回 AP-only。
**下载升级**：停 SoftAP + 停 BLE 释放射频，STA 独占连路由器，HTTPS 下载固件，完成后重启。

### 2.4 升级交互流程

```
用户进入配网页 /ota
  → 看到现有"上传升级"卡片 + 新增"在线检查更新"卡片
  → 输入家庭 WiFi SSID/PSK（自动回填已保存值）
  → 点击"检查更新"
    → POST /ota/check（APSTA 连路由器 + 拉 latest.json + 返回 JSON）
    → 显示最新版本号/更新说明/是否有更新
  → 若有更新，点击"下载并升级"
    → POST /ota/upgrade（保存凭证 + 返回升级提示页 + 异步任务启动）
    → 异步任务：延迟 3s → 停 AP+BLE → 启 STA → 检查版本 → HTTPS 下载 → 重启
  → 用户看到"正在下载升级"提示页，约 2 分钟后设备重启
  → 重新连接蓝牙，进入新固件
```

### 2.5 模块实现

#### 新增模块

| 文件 | 职责 |
|------|------|
| `main/ota_downloader.h/.c` | HTTPS 检查版本 + 下载升级（esp_http_client + esp_https_ota） |
| `ota/latest.json` | 版本信息文件（放仓库，raw 访问） |

#### 修改模块

| 文件 | 变更 |
|------|------|
| `main/radio_ctrl.h/.c` | 新增 `radio_ctrl_sta_connect`/`sta_disconnect`/`enter_sta_exclusive` |
| `main/nvs_store.h/.c` | 新增 `nvs_store_save_wifi_cred`/`load_wifi_cred`（独立 namespace "wifica"） |
| `main/wifi_portal.c` | 新增 `/ota/check` 和 `/ota/upgrade` 路由 + 前端 UI 卡片 |
| `main/bikepower_config.h` | 新增 OTA 下载/家庭 WiFi 常量 + `FIRMWARE_VERSION` 改 3.1.6 |
| `main/CMakeLists.txt` | 加 `ota_downloader.c` + `esp_https_ota`/`esp_http_client` 依赖 |

### 2.6 关键常量

```c
#define OTA_VERSION_URL        "https://gitee.com/mengen4jv/bikepower-c/raw/main/ota/latest.json"
#define OTA_DOWNLOAD_TIMEOUT_MS  30000   // HTTPS 单次请求超时
#define OTA_STA_CONNECT_MS       15000   // STA 连路由器超时
#define OTA_BUF_SIZE             4096    // HTTPS 下载缓冲
#define NVS_NAMESPACE_WIFI     "wifica"  // 家庭 WiFi 凭证独立 namespace
```

### 2.7 latest.json 格式

```json
{"version":"3.1.6","url":"https://gitee.com/.../bikepower-c.bin","size":1175760,"changelog":"更新说明"}
```

字段说明：
- `version`：最新版本号（字符串比较，与 `FIRMWARE_VERSION` 不同即为有更新）
- `url`：固件 bin 的 HTTPS 下载 URL
- `size`：固件大小（字节，仅展示用）
- `changelog`：更新说明（展示给用户）

### 2.8 内存预算

| 场景 | v3.1.5 | v3.1.6 | 说明 |
|------|--------|--------|------|
| 配网态（APSTA 共存检查版本） | - | ~100 KB | AP+STA+BLE 三栈共存 |
| 下载升级态（STA 独占） | - | ~150 KB+ | BLE 已停，释放射频 |

**结论**：检查版本时 APSTA+BLE 共存约 100KB（高于 80KB 告警线）；下载升级时 BLE 已停，内存充足。

### 2.9 风险与已知限制

| 风险 | 级别 | 缓解 |
|------|------|------|
| Gitee raw URL 访问限流 | 低 | 公开仓库无频率限制 |
| 家庭 WiFi 信号弱导致下载失败 | 中 | 超时回退重启回 BLE |
| 配网页 SSID/PSK 未做 XSS 转义 | 中 | 已知限制，阶段5统一处理 |
| 下载中断电变砖 | 中 | OTA 分区双备份，可回滚 |
| 版本号比较用字符串不等 | 低 | 仅判断有无更新，不做降级保护 |

### 2.10 验证计划

| 验证项 | 方法 | 通过标准 |
|--------|------|---------|
| 检查版本 | 配网页输入家庭WiFi，点检查更新 | 返回最新版本号和更新说明 |
| STA 独占下载 | 点下载并升级，观察串口日志 | 下载完成+重启进入新固件 |
| 凭证持久化 | 重启后再次进入配网页 | SSID 自动回填 |
| 失败回退 | 输入错误 WiFi 密码 | 检查失败返回错误 JSON，配网页可用 |
| 下载中断电 | 下载过程中断电 | 重启后回旧固件，可重新升级 |

---

## 三、FTMS 扩展方案

### 2.1 现状分析

当前 FTMS 服务（0x1826）已实现 4 个特征：

| 特征 | UUID | 类型 | 状态 |
|------|------|------|------|
| Fitness Machine Feature | 0x2ACC | 只读 | ✅ 已实现 |
| Indoor Bike Data | 0x2AD2 | 通知 | ✅ 已实现 |
| Fitness Machine Control Point | 0x2AD9 | 写+通知 | ✅ 已实现 |
| Fitness Machine Status | 0x2ADA | 通知 | ✅ 已实现 |

Control Point 已支持指令：

| 指令 | OpCode | 状态 |
|------|--------|------|
| Request Control | 0x00 | ✅ |
| Set Target Power | 0x05 | ✅（ERG 模式） |
| Start/Resume | 0x07 | ✅ |
| Stop/Pause | 0x08 | ✅ |
| Reset | 0x01 | ✅ |

### 2.2 扩展目标

补全 FTMS 规范中常用特征和指令，提升 Zwift、TrainerRoad、Rouvy 等主流骑行 App 兼容性。

### 2.3 功能清单

#### P0：Control Point 指令扩展

| 指令 | OpCode | 参数 | 说明 |
|------|--------|------|------|
| Set Target Inclination | 0x03 | sint16（-40.0%~40.0%，0.1% 步进） | 坡度模拟，影响功率计算 |
| Set Target Resistance Level | 0x04 | uint8（0-100） | 阻力等级 |
| Set Target Heart Rate | 0x06 | uint8（0-220 bpm） | 心率目标（可选） |

**实现要点**：
- 坡度/阻力指令映射到功率引擎的阻力系数，影响 base_power 计算
- 新增 `power_engine_set_inclination()` / `power_engine_set_resistance()` 接口
- 范围校验在领域层，超出范围返回 `FTMS_RESULT_INVALID_PARAM`

#### P1：只读特征补充

| 特征 | UUID | 内容 | 说明 |
|------|------|------|------|
| Supported Power Range | 0x2AD8 | min/max/inc（各 sint16） | 声明支持功率范围（POWER_MIN~POWER_MAX） |
| Supported Resistance Level Range | 0x2AD6 | min/max/inc（各 uint8） | 声明阻力范围（0-100） |

**实现要点**：
- 静态只读特征，值从 `domain_config.h` 常量生成
- 仅需注册 GATT 特征 + 读回调，无通知逻辑

#### P2：Training Status 字符串

| 特征 | UUID | 内容 | 说明 |
|------|------|------|------|
| Training Status | 0x2AD3 | status + string | 训练状态字符串（Idle/Running/Paused） |

**实现要点**：
- 与现有 Status 特征（0x2ADA）不同，0x2AD3 携带可读字符串
- 状态变化时通知（Start/Stop/Pause/Reset 触发）

### 2.4 内存预算

| 新增特征 | RAM 增量 | Flash 增量 |
|---------|---------|-----------|
| 2 个只读特征（Power Range + Resistance Range） | ~200 B | ~600 B |
| Training Status 字符串 | ~100 B | ~400 B |
| Control Point 指令扩展（无新特征） | 0 B | ~300 B |
| **合计** | **~300 B** | **~1.3 KB** |

**结论**：内存影响可忽略，满负载 117KB → 116.7KB。

### 2.5 兼容性评估

| App | 当前兼容性 | 扩展后 | 说明 |
|-----|-----------|--------|------|
| Zwift | ✅ 基本可用 | ✅ 更好 | 坡度指令提升 ERG+坡度模式切换体验 |
| TrainerRoad | ✅ ERG 可用 | ✅ 完整 | 阻力等级支持完善结构化训练 |
| Rouvy | ✅ 基本可用 | ✅ 更好 | 坡度模拟增强视频骑行沉浸感 |
| Wahoo Fitness | ✅ 基本可用 | ✅ 完整 | Supported Range 特征避免越界请求 |

### 2.6 风险

| 风险 | 级别 | 缓解 |
|------|------|------|
| 坡度→功率映射算法需调优 | 中 | 参考 FTMS 规范 + 实测校准 |
| 阻力等级语义不统一（各厂商） | 低 | 遵循 FTMS 规范 0-100 线性映射 |
| App 对新特征行为不一 | 低 | 真机测试多 App 验证 |

---

## 四、内存优化方案

### 3.1 优化目标

| 目标 | 当前 | 目标 | 方法 |
|------|------|------|------|
| WiFi 残留内存 | -48 KB | -20 KB | WiFi 资源彻底释放 |
| 配网态峰值 | 117 KB | 130 KB+ | HTTP 分块 + 缓冲复用 |
| BLE 通知缓冲 | 预分配 | 不变 | 已优化，维持 |

### 3.2 方案 A：WiFi 残留内存释放（P0）

**问题**：配网结束后 `esp_wifi_deinit()` 不完全释放内存，残留 48KB。

**方案**：
1. 配网结束时调用 `esp_wifi_stop()` + `esp_wifi_deinit()` + `esp_event_loop_delete_default()`
2. 手动释放 netif 和 DHCP server
3. 评估是否需要 `esp_netif_destroy_default_wifi_ap()`

**预期收益**：残留从 48KB 降至 20KB 以内（~28KB 回收）

**风险评估**：
- `esp_event_loop_delete_default()` 可能影响其他事件订阅，需验证 BLE 事件不受影响
- 复杂度中等，需真机测试多次配网稳定性

**内存预算**：净收益 +28KB

### 3.3 方案 B：HTTP 响应缓冲复用（P1）

**问题**：配网页面 HTML 每次请求动态生成，存在临时缓冲分配。

**方案**：
1. HTML 模板预编译为静态字符串（PROGMEM 等效）
2. 动态字段用 `httpd_resp_send_chunk` 分块发送
3. 复用单一全局缓冲，避免重复 malloc

**预期收益**：配网态峰值降 ~5KB

**内存预算**：净收益 +5KB

### 3.4 方案 C：BLE 通知频率自适应（P2）

**问题**：BLE 通知固定 50ms 周期，连接态占用带宽和 CPU。

**方案**：
1. 数据变化时通知（功率/心率/踏频变化超过阈值才发）
2. 静止状态降频（1s 周期）
3. 运动状态恢复 50ms

**预期收益**：CPU 占用降低，间接降低栈使用

**风险评估**：可能影响 App 数据平滑度，需 App 端配合测试

### 3.5 方案对比

| 方案 | 收益 | 复杂度 | 风险 | 推荐 |
|------|------|--------|------|------|
| A: WiFi 残留释放 | +28KB | 中 | 中（事件循环） | ✅ P0 |
| B: HTTP 缓冲复用 | +5KB | 低 | 低 | ✅ P1 |
| C: 通知自适应 | CPU 降 | 中 | 中（App 兼容） | ⚠️ P2 可选 |

---

## 五、迭代功能清单汇总

### 4.1 P0（必须）

| # | 功能 | 模块 | 预估 |
|---|------|------|------|
| 1 | Set Target Inclination 指令 | ble_bikepower.c + power_engine | 坡度→功率映射 |
| 2 | Set Target Resistance Level 指令 | ble_bikepower.c + power_engine | 阻力→功率映射 |
| 3 | WiFi 残留内存释放 | radio_ctrl.c | deinit 链路调优 |

### 4.2 P1（应该）

| # | 功能 | 模块 | 预估 |
|---|------|------|------|
| 4 | Supported Power Range 特征 | ble_bikepower.c | 静态只读 |
| 5 | Supported Resistance Level Range 特征 | ble_bikepower.c | 静态只读 |
| 6 | HTTP 响应缓冲复用 | wifi_portal.c | 分块+复用 |

### 4.3 P2（可选）

| # | 功能 | 模块 | 预估 |
|---|------|------|------|
| 7 | Training Status 字符串特征 | ble_bikepower.c | 状态通知 |
| 8 | Set Target Heart Rate 指令 | ble_bikepower.c + hr_model | 心率目标 |
| 9 | BLE 通知频率自适应 | ble_bikepower.c | 变化检测 |

---

## 六、内存安全边界预测

```
v3.1.5 满负载：                    117 KB
+ FTMS 扩展（特征+指令）：          -0.3 KB → 116.7 KB
+ 方案 A（WiFi 残留释放）：         +28 KB  → 144.7 KB（配网后）
+ 方案 B（HTTP 缓冲复用）：         +5 KB   → 149.7 KB（配网态峰值）
```

**结论**：v3.1.6 全部 P0+P1 完成后，满负载剩余约 150KB，远超 80KB 告警线，为后续版本（BLE 配对加密、训练记录等）预留充足空间。

---

## 七、验证计划

### 6.1 FTMS 扩展验证

| 验证项 | 方法 | 通过标准 |
|--------|------|---------|
| 坡度指令 | Zwift 发送坡度，观察功率变化 | 功率随坡度合理变化 |
| 阻力指令 | TrainerRoad 发送阻力等级 | 功率随阻力合理变化 |
| Supported Range 读取 | bleak 读取特征值 | 返回正确的 min/max/inc |
| 多 App 兼容 | Zwift + TrainerRoad + Rouvy 轮测 | 无异常断连/错误 |

### 6.2 内存优化验证

| 验证项 | 方法 | 通过标准 |
|--------|------|---------|
| WiFi 残留 | 配网结束后读心跳堆 | ≥148KB（较 v3.1.5 +28KB） |
| 多次配网 | 连续 3 次配网，每次读堆 | 残留不累积 |
| 配网态峰值 | 配网+手机连接时读堆 | ≥130KB |
| 稳定性 | 连续 5 次配网+重启 | 无崩溃/看门狗 |

---

## 八、发版节奏

| 阶段 | 内容 | 产出 |
|------|------|------|
| 阶段 10a | FTMS 指令扩展（P0 #1-#2） | 编译+真机验证 |
| 阶段 10b | FTMS 只读特征（P1 #4-#5） | 编译+真机验证 |
| 阶段 10c | WiFi 残留释放（P0 #3） | 真机内存对比 |
| 阶段 10d | HTTP 缓冲复用（P1 #6） | 真机内存对比 |
| 阶段 10e | 集成测试 + 多 App 兼容测试 | 测试报告 |
| 发版 | 打 tag v3.1.6 + merge_bin | Release |

---

## 九、相关文档

| 文档 | 说明 |
|------|------|
| [内存与新功能评估](09-memory-and-feature-assessment.md) | v3.1.5 内存基线与可行性分析 |
| [技术参考](04-technical-reference.md) | BLE 服务与数据格式详解 |
| [OTA 落地方案](10-ota-implementation-plan.md) | OTA 实现细节 |
