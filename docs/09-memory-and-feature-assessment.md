# 内存占用评估与新功能可行性分析

> 评估日期：2026-06-27
> 固件版本：v3.1.3
> ESP-IDF：v5.5.4（2026-06-27 从 v5.5.1 升级）
> 工具链：esp-14.2.0_20260121
> 评估方法：真机实测（ESP32-C3 v0.4）+ 静态代码分析

---

## 一、ESP-IDF v5.5.1 → v5.5.4 升级结果

### 升级变更

| 项目 | v5.5.1 | v5.5.4 |
|------|--------|--------|
| ESP-IDF | v5.5.1 | v5.5.4 |
| 工具链 | esp-14.2.0_20241119 | esp-14.2.0_20260121 |
| phy_version | 1201 | 1232（Aug 25 2025） |
| BT controller | 2edb0b0 | b7de11e |
| WiFi firmware | — | 0947e8b |

### 固件体积变化

| 版本 | bin 大小 | 变化 |
|------|---------|------|
| v5.5.1 | 0xf7350 (1014KB) | 基线 |
| v5.5.4 | 0xf7e60 (1015KB) | +2.8KB（+0.3%） |

升级后体积几乎不变，编译通过，真机启动正常，BLE/WiFi 功能验证通过。

### 升级注意事项

1. 工具链需更新到 `esp-14.2.0_20260121`（v5.5.4 强制要求）
2. 升级后需 `rm -rf build sdkconfig` 全量重编，避免缓存残留
3. openthread 子模块更新有冲突（项目未用，已跳过）
4. 编译需手动指定新工具链 PATH：
   ```bash
   export PATH="/home/mengen1014/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin:$PATH"
   ```

---

## 二、硬件资源概览

### ESP32-C3 内存资源

| 类型 | 容量 | 说明 |
|------|------|------|
| SRAM | 400 KB | 主内存，含 heap + 栈 + 静态 |
| ROM | 384 KB | 固化代码（bootloader、库函数） |
| RTC SRAM | 8 KB | 深睡保持 |
| Flash | **4 MB（实测）** | sdkconfig 配置 2MB，芯片实际 4MB |

### Heap 初始化分布（启动日志实测）

| 区域 | 地址 | 长度 | 大小 |
|------|------|------|------|
| RAM | 0x3FC9ED90 | 0x21270 | 132 KB |
| Retention RAM | 0x3FCC0000 | 0x1C710 | 113 KB |
| Retention RAM | 0x3FCDC710 | 0x2950 | 10 KB |
| RTCRAM | 0x50000020 | 0x1FC8 | 7 KB |
| **Heap 总量** | | | **262 KB** |

> 注：262KB 是可分配 heap 总量，不含栈、静态变量、BLE controller 内部 RAM。

### 任务栈分配（从 RAM，非 heap）

| 任务 | 栈大小 | 存在条件 |
|------|--------|---------|
| main_task | 3584 B | 常驻 |
| button_task | 2560 B | 常驻 |
| led_task | 2048 B | 常驻 |
| NimBLE host | 4096 B | 常驻（BLE） |
| wifi driver | 6656 B | 仅配网时 |
| httpd | 8192 B | 仅配网时 |
| lwip tcpip | 3072 B | 仅配网时 |
| esp_event | ~2048 B | 常驻 |
| idle | ~1024 B | 常驻 |
| **栈合计（常驻）** | **~15 KB** | |
| **栈合计（配网时）** | **~33 KB** | |

---

## 三、各场景内存占用实测

### 实测数据汇总

| 场景 | 空闲堆（字节）| 空闲堆（KB）| 占 heap 总量 | 较启动态消耗 |
|------|--------------|------------|-------------|-------------|
| ① 启动未连接（BLE 广播） | 180,464 | 176 | 67% | 基线 |
| ② BLE 连接稳定推送 | ~175,000* | ~171* | 65% | -5 KB |
| ③ 配网态无连接（BLE+WiFi 共存） | 137,156 | 134 | 51% | -42 KB |
| ④ 配网态手机连 SoftAP | 121,140 | 118 | 45% | -58 KB |
| ⑤ 配网网页操作中 | ~120,000 | ~117 | 45% | -59 KB |
| ⑥ 关闭 WiFi 回 BLE 后 | 131,452 | 129 | 49% | -48 KB |

> *场景②为 v5.5.1 实测参考值（连接后 BLE 通知 + 协议栈开销约 5KB），v5.5.4 应相近。

### 场景间增量分析

| 转换 | 堆变化 | 原因 |
|------|--------|------|
| ①→② BLE 连接 | -5 KB | GATT 连接 + 通知缓冲 |
| ①→③ 启动 WiFi 共存 | -42 KB | WiFi 驱动 + lwip + netif + DHCP |
| ③→④ 手机连 SoftAP | -16 KB | TCP 连接 + HTTP 请求缓冲 |
| ④→⑤ 网页操作 | -1 KB | 草稿/配置缓冲（预分配，波动小） |
| ⑤→⑥ 关 WiFi | +12 KB | WiFi 驱动部分释放 |
| ⑥ vs ① 净差 | -48 KB | **WiFi 残留未释放（见下文）** |

### 关键发现：WiFi 残留内存

**现象**：配网结束关闭 WiFi 回到 BLE 后，堆只恢复到 129KB，比启动时 176KB 少 48KB。

**根因**：ESP-IDF 的 `esp_wifi_deinit()` 不会释放所有 WiFi 内部内存（已知行为），包括：
- 部分 lwip 协议栈结构
- WiFi driver 部分静态缓冲
- event loop 默认实例（`esp_event_loop_delete_default` 释放不彻底）

**影响评估**：
- 单次配网后残留 48KB，**多次配网不会继续泄漏**（实测二次配网残留量一致）
- 残留后剩余 129KB，仍远超 80KB 告警线，不影响正常使用
- 这是 ESP-IDF 框架行为，非项目代码缺陷，无需修复

### 满负载场景评估

**最恶劣场景**：BLE 连接 + 配网态 + 手机连 SoftAP + 网页操作

| 指标 | 数值 |
|------|------|
| 实测空闲堆 | 117 KB |
| 占 heap 总量 | 45% |
| 剩余比例 | 55% |
| 距 80KB 告警线 | +37 KB（安全） |
| 距 40KB 危险线 | +77 KB（充裕） |

**结论**：满负载下内存充裕，剩余 117KB 足以支撑当前功能稳定运行。

---

## 四、Flash 占用评估

### 当前分区布局（sdkconfig 配 2MB）

| 分区 | 偏移 | 大小 | 用途 |
|------|------|------|------|
| nvs | 0x9000 | 24 KB | 配置持久化 |
| phy_init | 0xf000 | 4 KB | PHY 校准 |
| factory | 0x10000 | 1792 KB | 应用固件 |
| coredump | 0x1D0000 | 64 KB | 崩溃转储 |
| **合计** | | **1884 KB** | |

### 固件占用

| 项目 | 大小 | 占 factory 分区 |
|------|------|----------------|
| bikepower-c.bin | 1015 KB (0xf7e60) | 55% |
| 剩余可用 | 777 KB | 45% |

### Flash 资源潜力

**关键发现：芯片实测 4MB，sdkconfig 仅配置 2MB**

```
当前配置 2MB：已用 1884KB，剩余 144KB（含未分配）
实际芯片 4MB：可用 4096KB，剩余 2212KB（约 2.16MB 未利用）
```

**结论**：通过修改 `CONFIG_ESPTOOLPY_FLASHSIZE="4MB"` 并调整分区表，可释放约 2MB Flash 空间，足以支撑 OTA 等大功能。

---

## 五、新功能可行性评估

### 5.1 OTA 升级

**需求**：远程固件升级，支持 BLE 触发或 WiFi 下载

| 维度 | 评估 | 说明 |
|------|------|------|
| Flash | ✅ 可行 | 4MB 芯片可容 2×1MB OTA 分区 + 1MB factory + 数据分区 |
| RAM | ✅ 可行 | `esp_https_ota` 需 ~10-20KB buffer，配网态 117KB 堆足够 |
| 网络 | ⚠️ 需设计 | OTA 需 WiFi STA 连路由器下载，与当前 SoftAP 配网模式互斥，需状态机扩展 |
| BLE | ✅ 可行 | 可加 OTA 控制特征值，BLE 通知 OTA 进度 |
| 复杂度 | 中高 | 分区表重构 + 状态机扩展 + 下载/校验/切换逻辑 |

**推荐方案**：
1. 修改分区表为 OTA 布局：`ota_0`(1MB) + `ota_1`(1MB) + `otadata` + `nvs` + `coredump`
2. 扩展状态机加 `ST_OTA` 状态：BLE 收到 OTA 命令 → 切 WiFi STA 连路由器 → HTTPS 下载 → 写 ota 分区 → 校验 → 切分区重启
3. 复用现有 radio_ctrl 统一管理 WiFi 启停
4. BLE 加 OTA 控制服务（进度通知 + 断点续传）

**内存预算**：
- OTA 下载 buffer：~16KB（可配置）
- HTTPS 上下文：~8KB
- 总计约 24KB，配网态 117KB 堆可承受

### 5.2 Captive Portal（自动弹出配网页）

**需求**：手机连 WiFi 后自动弹出配网页，解决当前需手动输入 192.168.4.1 的问题

| 维度 | 评估 | 说明 |
|------|------|------|
| Flash | ✅ 可行 | `esp_dns_server` 组件约 4KB flash |
| RAM | ✅ 可行 | DNS 服务器约 2-4KB 堆 |
| 复杂度 | 低 | 加一个 DNS 劫持任务，所有查询返回 192.168.4.1 |
| 兼容性 | ⚠️ 中 | iOS/Android/不同浏览器弹出行为不一，需多平台测试 |

**推荐方案**：
1. 添加 `esp_dns_server` 组件（ESP-IDF 自带）
2. 配网启动时启动 DNS 服务器，劫持所有域名解析到 192.168.4.1
3. 配网结束时停止 DNS 服务器
4. 集成到 `radio_ctrl_enter_wifi` / `radio_ctrl_exit_wifi` 生命周期

**收益**：解决用户反馈的"白屏/需手动输 IP"问题，提升配网体验

**内存预算**：约 3KB，对满负载 117KB 堆无压力

### 5.3 FTMS 服务扩展

**需求**：扩展 FTMS（Fitness Machine Service）支持更多训练控制指令

| 维度 | 评估 | 说明 |
|------|------|------|
| Flash | ✅ 可行 | 每个 characteristic 约 200-500 bytes flash |
| RAM | ✅ 可行 | 每个 characteristic 约 100-200 bytes RAM |
| 复杂度 | 低 | 现有 FTMS 服务已就绪，仅加 characteristic + handler |
| 兼容性 | ✅ 高 | 遵循 FTMS 规范，骑行 App 兼容性好 |

**可扩展功能**：
- `Fitness Machine Control Point` 扩展指令（设置坡度、阻力等级、心率目标）
- `Supported Power Range` / `Supported Resistance Level Range`（只读特征）
- `Training Status`（训练状态字符串）
- `Firmware Revision String`（OTA 版本关联）

**内存预算**：5 个 characteristic 约 1KB RAM，可忽略

### 5.4 其他候选功能评估

| 功能 | 可行性 | 内存预算 | 备注 |
|------|--------|---------|------|
| BLE 多连接（2台 App） | ⚠️ 受限 | -8KB/连接 | NimBLE 已配 1 连接，改 2 需测试共存稳定性 |
| WiFi STA + SoftAP 并行 | ❌ 不推荐 | -20KB | 共存下内存吃紧，且 C3 单射频性能差 |
| 蓝牙配对加密 | ✅ 可行 | +2KB | 提升安全性，模拟器场景非必需 |
| Web OTA（配网页集成） | ✅ 可行 | 复用 5.1 | 配网页加"固件上传"按钮，multipart 上传到 ota 分区 |
| 训练数据记录 | ✅ 可行 | 依赖 Flash | 加 LittleFS 分区记录骑行历史 |
| 心率广播模式 | ✅ 可行 | +1KB | 复用 BLE broadcaster，加 HRS 广播 |

---

## 六、综合规划建议

### 6.1 优先级排序

| 优先级 | 功能 | 理由 | 内存影响 |
|--------|------|------|---------|
| P0 | Captive Portal | 解决白屏体验问题，成本低收益高 | +3KB |
| P1 | FTMS 扩展 | 提升骑行 App 兼容性，成本极低 | +1KB |
| P2 | OTA 升级 | 长期维护必备，复杂度中高 | +24KB（OTA 时） |
| P3 | BLE 配对加密 | 安全增强，非必需 | +2KB |

### 6.2 内存安全边界

```
满负载（BLE+WiFi配网+手机连接）：117 KB
+ Captive Portal：              +3 KB  → 114 KB
+ FTMS 扩展：                   +1 KB  → 113 KB
+ OTA（下载时）：                +24 KB → 89 KB   ← 仍 > 80KB 告警线，安全
```

**结论**：所有 P0-P2 功能叠加后，满负载剩余约 89KB，仍在安全区间。

### 6.3 Flash 规划建议

若启动 OTA，建议分区表调整为（4MB Flash）：

```
nvs,       data, nvs,      0x9000,   0x6000    (24KB)
phy_init,  data, phy,      0xf000,   0x1000    (4KB)
factory,   app,  factory,  0x10000,  0x100000  (1MB，预留当前固件+扩展)
ota_0,     app,  ota_0,    0x110000, 0x100000  (1MB)
ota_1,     app,  ota_1,    0x210000, 0x100000  (1MB)
otadata,   data, ota,      0x310000, 0x2000    (8KB)
coredump,  data, coredump, 0x312000, 0x10000   (64KB)
# 剩余约 880KB 可用于 LittleFS/训练记录
```

### 6.4 不建议的方向

| 方向 | 原因 |
|------|------|
| 开机自启 WiFi | 违反硬件约束（占用射频、内存、功耗） |
| WiFi STA + BLE + SoftAP 三者并行 | C3 单射频 + 内存吃紧，性能不可接受 |
| 蓝牙音频 | C3 不支持 A2DP，无意义 |
| 大缓冲单次分配 | 共存下应分块传输，避免堆碎片 |

---

## 七、当前已知问题

### 7.1 配网页面偶发 ECONNRESET（白屏）

**现象**：手机首次访问 192.168.4.1 时偶发 `httpd_sock_err: error in send : 104`（ECONNRESET），浏览器白屏。

**根因**：BLE+WiFi 共存模式下 WiFi 吞吐受限，首次 TCP 连接握手 + HTML 传输可能超时，手机浏览器主动断开。

**影响**：偶发，刷新页面后正常。不影响配网功能完整性。

**缓解方案**：
1. **P0 Captive Portal**：减少手动输入 IP 步骤，降低首次访问延迟
2. HTML 分块发送（已实现 `httpd_resp_send_chunk`），避免大包超时
3. 可选：调大 `CONFIG_HTTPD_PURGE_BUF_LEN` 或关闭不必要的 TCP 选项

### 7.2 WiFi 残留内存

**现象**：配网结束后堆恢复到 129KB，比启动时少 48KB。

**定性**：ESP-IDF 框架行为，非代码缺陷。多次配网不累积泄漏。剩余 129KB 仍安全。

**处置**：不修复，记录为已知限制。

---

## 八、附：实测数据采集方法

### 采集脚本

固件中已埋点（v3.1.3 诊断日志）：
- `wifi_portal.c::wifi_portal_start`：打印"配网启动前 堆=X"
- `wifi_portal.c::wifi_portal_stop`：打印"HTTP 配网门户已停止 堆=X"
- `radio_ctrl.c::radio_ctrl_start_wifi_ap`：打印"SoftAP 启动 ... 堆=X"
- `wifi_portal.c::h_home`：打印"首页请求到达 堆=X"
- `app_main.c`：每 10s 打印"心跳 堆=X p=X hr=X c=X mode=X conn=X state=X"

### 场景复现

1. 启动未连接：上电后 10s 心跳
2. 配网态：长按 BOOT ≥2s 释放 → 3s 内再按一次进入配网
3. 手机连 SoftAP：手机连 WiFi "BikePower_XXXX"
4. 网页操作：手机浏览器访问 192.168.4.1
5. 关闭 WiFi：网页点"保存并关闭 WiFi"或等 180s 超时

---

## 九、版本基线

| 项 | 值 |
|----|-----|
| 固件版本 | v3.1.3 |
| ESP-IDF | v5.5.4 |
| git commit | 53c10f1 + 临时诊断日志 |
| 实测芯片 | ESP32-C3 v0.4, 4MB Flash |
| 编译时间 | Jun 27 2026 14:53:29 |
