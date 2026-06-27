# v3.1.6 迭代规划：FTMS 扩展与内存优化

> 规划日期：2026-06-27
> 当前版本：v3.1.5（OTA + Captive Portal 已完成）
> 目标版本：v3.1.6
> 状态：规划中

---

## 一、迭代背景

v3.1.5 已完成 OTA 升级和 Captive Portal 自动弹配网页，核心功能稳定。v3.1.6 聚焦两个方向：

1. **FTMS 扩展**：提升骑行 App 兼容性，补全 FTMS 规范特征
2. **内存优化**：降低配网态内存占用，为后续功能腾出空间

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

## 二、FTMS 扩展方案

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

## 三、内存优化方案

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

## 四、迭代功能清单汇总

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

## 五、内存安全边界预测

```
v3.1.5 满负载：                    117 KB
+ FTMS 扩展（特征+指令）：          -0.3 KB → 116.7 KB
+ 方案 A（WiFi 残留释放）：         +28 KB  → 144.7 KB（配网后）
+ 方案 B（HTTP 缓冲复用）：         +5 KB   → 149.7 KB（配网态峰值）
```

**结论**：v3.1.6 全部 P0+P1 完成后，满负载剩余约 150KB，远超 80KB 告警线，为后续版本（BLE 配对加密、训练记录等）预留充足空间。

---

## 六、验证计划

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

## 七、发版节奏

| 阶段 | 内容 | 产出 |
|------|------|------|
| 阶段 10a | FTMS 指令扩展（P0 #1-#2） | 编译+真机验证 |
| 阶段 10b | FTMS 只读特征（P1 #4-#5） | 编译+真机验证 |
| 阶段 10c | WiFi 残留释放（P0 #3） | 真机内存对比 |
| 阶段 10d | HTTP 缓冲复用（P1 #6） | 真机内存对比 |
| 阶段 10e | 集成测试 + 多 App 兼容测试 | 测试报告 |
| 发版 | 打 tag v3.1.6 + merge_bin | Release |

---

## 八、相关文档

| 文档 | 说明 |
|------|------|
| [内存与新功能评估](09-memory-and-feature-assessment.md) | v3.1.5 内存基线与可行性分析 |
| [技术参考](04-technical-reference.md) | BLE 服务与数据格式详解 |
| [OTA 落地方案](10-ota-implementation-plan.md) | OTA 实现细节 |
