# OTA 升级与 Captive Portal 落地方案

> 🗄️ **历史 / 归档文档，仅供追溯**：本文记录 v3.1.4 规划阶段「配网页上传固件」方案，已被 v3.1.6 起的「HTTPS 在线下载」取代。当前 OTA 实际流程以 [OTA 固件升级操作手册](../user/04-ota-user-guide.md) 为准。

> ⚠️ 历史方案文档：本文记录 v3.1.4 规划阶段的设计，当时采用「配网页 multipart 上传固件」方式。
> v3.1.6 起 OTA 升级方式已演进为「HTTPS 在线下载」，移除 `/ota/upload` 上传入口，凭证统一在 `/more` 家庭网络配置。
> 当前实际流程以 [OTA 固件升级操作手册](../user/04-ota-user-guide.md) 为准，本文保留作历史参考。

> 固件版本：v3.1.4（规划）
> 硬件：ESP32-C3，4MB Flash
> 关联文档：[内存与新功能评估](04-memory-and-feature-assessment.md)、[技术参考](../dev/01-technical-reference.md)

---

## 一、总览

| 功能 | 触发方式 | 状态 | 优先级 |
|------|---------|------|--------|
| Captive Portal（自动弹配网页） | 手机连 SoftAP 自动弹出 | ✅ 已实施（v3.1.4） | P0 |
| OTA 升级（配网页上传固件） | 配网页"更多配置"→选 bin 上传 | ✅ 已实施（v3.1.5） | P0 |

技术路线决策（已与用户确认）：
- Captive Portal：自研轻量 DNS 服务器（UDP 53 劫持），无第三方依赖
- OTA：配网页 multipart 上传固件到 OTA 分区，复用现有 SoftAP，无需扩 WiFi STA 状态
- 分区表：4MB Flash，ota_0/ota_1 互切（各 1.75MB）+ otadata，已调整到位

---

## 二、Captive Portal 实施总结（已完成）

### 2.1 实现方案

自研轻量 DNS 服务器 + OS 探测端点重定向，零第三方依赖，符合项目"结构扁平是美德"原则。

| 组件 | 文件 | 职责 |
|------|------|------|
| DNS 服务器 | [main/captive_portal.c](../../main/captive_portal.c) | UDP 53 监听，所有 A 记录查询应答 192.168.4.1 |
| 探测 handler | [main/captive_portal.c](../../main/captive_portal.c) | 302 重定向到 http://192.168.4.1/ |
| 生命周期集成 | [main/radio_ctrl.c](../../main/radio_ctrl.c) | AP 启动后启动 DNS，退出配网时停止 |
| 路由注册 | [main/wifi_portal.c](../../main/wifi_portal.c) | 注册 10 个 OS 探测端点 |
| 常量 | [main/bikepower_config.h](../../main/bikepower_config.h) | CAPTIVE_PORTAL_HOST、HTTP_MAX_URI_HANDLERS |

### 2.2 覆盖的 OS 探测端点

| 端点 | 系统 | 原期望响应 | 本方案响应 |
|------|------|-----------|-----------|
| `/generate_204` | Android/Chrome | 204 No Content | 302 → 首页 |
| `/gen_204` | Android 变体 | 204 | 302 → 首页 |
| `/hotspot-detect.html` | iOS/macOS | Success 页 | 302 → 首页 |
| `/library/test/success.html` | iOS 变体 | Success 页 | 302 → 首页 |
| `/ncsi.txt` | Windows | NCSI 文本 | 302 → 首页 |
| `/connecttest.txt` | Windows 10+ | 连通文本 | 302 → 首页 |
| `/canonical.html` | Firefox | canonical 页 | 302 → 首页 |
| `/success.txt` | Firefox 变体 | success 文本 | 302 → 首页 |
| `/redirect` | 通用 | 重定向 | 302 → 首页 |
| `/kindle-wifi/wifistub.html` | Kindle | stub 页 | 302 → 首页 |

原理：各 OS 连上 WiFi 后会后台请求探测端点判断"是否有网"。正常返回期望内容=有网；返回 302 重定向=无网/需登录，系统自动弹出 Captive Portal 浏览器窗口加载重定向目标（即配网首页）。

### 2.3 DNS 服务器实现要点

- UDP socket bind 0.0.0.0:53
- 独立任务 `dns_server`（栈 3072 字节，优先级 5）
- 解析 DNS Query header，跳过 QNAME 定位 Question 末尾
- 构造响应：复制 Query + 追加 1 个 Answer（NAME 指针 0xC00C + TYPE A + CLASS IN + TTL 60 + RDATA 192.168.4.1）
- 只处理标准 Query（QR=0）且 QDCOUNT≥1，其他静默丢弃
- 幂等启停：`captive_portal_start_dns` 已运行直接返回，`captive_portal_stop_dns` 先置 sock=-1 再 close

### 2.4 资源占用

| 项 | 占用 |
|----|------|
| DNS 任务栈 | 3072 字节 |
| DNS UDP 缓冲 | 512 字节（栈上 rx/tx 各一） |
| 固件增量 | ~2KB（captive_portal.o） |
| 运行时堆 | DNS 任务创建 ~3.5KB |

配网态总内存仍 >80KB 告警线，安全。

### 2.5 集成流程

```
用户长按确认 → ST_WIFI_CONFIG
  → radio_ctrl_enter_wifi
    → radio_ctrl_start_wifi_ap（AP 就绪）
    → captive_portal_start_dns（DNS 劫持启动）  ← 新增
    → wifi_portal_start（HTTP 服务 + 探测端点就绪）
  手机连上 BikePower 热点
    → 系统发探测请求 → 302 重定向 → 自动弹出配网页
  配网完成/超时
  → radio_ctrl_exit_wifi
    → wifi_portal_stop
    → captive_portal_stop_dns  ← 新增
    → radio_ctrl_stop_wifi_ap
  → ST_BLE_ACTIVE
```

### 2.6 真机验证清单

- [ ] iOS 连上 BikePower 热点后自动弹出配网页
- [ ] Android 连上后自动弹出配网页
- [ ] Windows 连上后浏览器提示需登录
- [ ] 不弹窗时手动访问 http://192.168.4.1 仍可配网（降级兼容）
- [ ] 退出配网后 DNS 任务已停止（串口日志 "DNS 劫持服务器已停止"）

---

## 三、OTA 升级方案与实施总结

> **实施状态：v3.1.5 已完成，真机测试通过。** 下方方案条款均已落地。

**实施总结**：
- `main/ota_update.c` 实现 multipart/form-data 流式上传 → `esp_ota_write`，峰值缓冲 ~4KB，不整包驻留内存。
- 防降级：解析上传固件头 `esp_app_desc_t.version`，与运行时 `FIRMWARE_VERSION` 宏比较，`<` 拒绝、`==` 允许重刷、`>` 放行。
- **版本同步关键修复**：`CMakeLists.txt` 强制 `PROJECT_VER` 从 `bikepower_config.h` 提取 `FIRMWARE_VERSION`，避免 `git describe` 后缀导致 `esp_app_desc.version` 与宏不一致（曾出现 app_desc=3.1.4 而宏=3.1.5，防降级基准错乱）。修复后两者严格一致。
- 自检回滚：新固件 `ESP_OTA_IMG_PENDING_VERIFY`，BLE 启动失败立即 `esp_ota_mark_app_invalid_rollback`；10s 自检窗口到期 `esp_ota_mark_app_valid_cancel_rollback` 确认。
- 真机验证：v3.1.4→v3.1.5 升级成功，v3.1.5→v3.1.4 降级被拒，自检确认有效（设备持续运行 v3.1.5 未回滚）。

### 3.1 设计目标

| 目标 | 说明 |
|------|------|
| 触发方式 | 配网页"更多配置"→选固件 bin→上传，复用现有 SoftAP |
| 安全性 | 固件头校验（magic/chip）+ 版本号防降级 + 可选签名 |
| 可靠性 | 双分区互切 + 应用回滚（新固件自检失败自动回退） |
| 内存友好 | 流式写入 esp_ota_write，不全量缓冲 |
| 进度反馈 | 上传进度条 + 重启倒计时提示 |

### 3.2 分区表（已就绪）

```
# Name,    Type, SubType, Offset,   Size
nvs,       data, nvs,      0x9000,   0x6000     (24KB)
phy_init,  data, phy,      0xf000,   0x1000     (4KB)
ota_0,     app,  ota_0,    0x10000,  0x1C0000   (1.75MB)  ← 当前运行
ota_1,     app,  ota_1,    0x1D0000, 0x1C0000   (1.75MB)  ← OTA 写入目标
otadata,   data, ota,      0x390000, 0x2000     (8KB)
coredump,  data, coredump, 0x392000, 0x10000    (64KB)
```

- 固件当前 1017KB，每分区 1.75MB，45% 空闲，足够容纳未来扩展
- 首次烧录烧到 ota_0，otadata 为空时 bootloader 选第一个 ota 分区
- OTA 时写 ota_1 并切换启动，再 OTA 写 ota_0 切回，互不覆盖

### 3.3 配置项（已就绪）

`sdkconfig.defaults` 已启用：
```ini
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
CONFIG_ESPTOOLPY_FLASHSIZE="4MB"
```

回滚机制：新固件首次启动状态为 `ESP_OTA_IMG_PENDING_VERIFY`，应用须调 `esp_ota_mark_app_valid_cancel_rollback()` 确认；未确认即重启则 bootloader 自动回滚到上一可用分区。

### 3.4 OTA 协议与流程

#### 上传协议

```
POST /ota/upload HTTP/1.1
Content-Type: multipart/form-data; boundary=----WebKitFormBoundary
Content-Length: <固件大小 + 表单开销>

------WebKitFormBoundary
Content-Disposition: form-data; name="firmware"; filename="bikepower-c.bin"
Content-Type: application/octet-stream

<固件二进制>
------WebKitFormBoundary--
```

#### 服务端处理流程

```
1. httpd 收到 POST /ota/upload
2. 读取 Content-Length，校验 ≤ ota 分区大小（1.75MB）
3. esp_ota_get_next_update_partition 获取目标分区（ota_1 或 ota_0）
4. esp_ota_begin(target_partition, OTA_SIZE_UNKNOWN, &handle)
5. 流式接收 multipart body：
   - 解析 boundary，定位固件数据起始
   - 循环 httpd_req_recv 读取，esp_ota_write 写入
   - 检测到结束 boundary 停止
6. esp_ota_end(handle)（校验固件头：magic 0xE9、segment 数、chip_id=ESP32-C3）
7. esp_ota_set_boot_partition(target_partition)
8. 返回成功页（含 5s 重启倒计时）
9. esp_restart()
```

#### 新固件启动自检（app_main）

```
1. esp_ota_get_state_partition(running, &state)
2. if state == ESP_OTA_IMG_PENDING_VERIFY:
   a. 启动 BLE、初始化各模块（正常流程）
   b. 注册一个 10s 一次性定时器
   c. 10s 内未 abort 且 BLE 初始化成功 → esp_ota_mark_app_valid_cancel_rollback()
   d. 10s 内关键初始化失败 → esp_ota_mark_app_invalid_rollback_and_reboot()
3. else: 正常启动（已确认或出厂固件）
```

自检通过条件：
- BLE NimBLE 初始化成功并开始广播
- power_engine 快照正常
- 10s 内无 abort/panic

### 3.5 状态机扩展

不新增顶层状态，OTA 作为 ST_WIFI_CONFIG 内的子流程：

```
ST_WIFI_CONFIG
  ├─ 正常配网（现有）
  ├─ OTA 上传中（/ota/upload 处理，HTTP 阻塞直到完成）
  ├─ OTA 上传成功 → 返回成功页 + 5s 重启
  └─ OTA 上传失败 → 返回错误页，回到配网页可重试
```

OTA 进行中禁止：修改配置、关闭 WiFi、超时自动退出（在 h_ota_upload 入口 arm_deadline 续期，处理期间不触发超时）。

### 3.6 UI 交互

#### "更多配置"页增加入口

```
更多配置
  ├─ 个人信息
  ├─ 设备名
  └─ 固件升级  ← 新增
      ├─ 当前版本：v3.1.4
      ├─ [选择文件] bikepower-c.bin
      └─ [开始升级]
```

#### 上传中页面

```
固件升级中...
进度：████████░░ 80%
请勿断开电源和 WiFi
```

#### 上传成功页面

```
固件升级成功！
设备将在 5 秒后重启进入新固件...
```

#### 上传失败页面

```
升级失败：<错误原因>
[返回重试]
```

### 3.7 错误处理

| 错误 | 处理 |
|------|------|
| 固件 > 分区大小 | 返回 413，提示文件过大 |
| magic byte 非 0xE9 | esp_ota_end 失败，返回错误页 |
| chip_id 不匹配 | esp_ota_end 失败，返回错误页 |
| 写入中途断连 | esp_ota_abort，返回错误页，分区数据废弃 |
| esp_ota_begin 失败 | 返回错误页，不切换启动分区 |
| 新固件自检失败 | bootloader 自动回滚到上一固件 |

### 3.8 安全考虑

| 项 | 当前方案 | 后续增强 |
|----|---------|---------|
| 固件来源 | 配网页本地上传，用户可信 | — |
| 固件完整性 | ESP-IDF 内置 sha256 校验 | 可选：应用层 RSA 签名 |
| 防降级 | 比较版本号，拒绝 ≤ 当前版本 | 可选：维护"已知坏版本"列表 |
| 传输安全 | SoftAP 本地，无中间人 | — |
| Flash 保护 | esp_ota_end 校验，写错分区不可能 | — |

### 3.9 内存预算

| 项 | 占用 |
|----|------|
| multipart 解析缓冲 | 1-2KB（栈上） |
| esp_ota_write 流式 | 无全量缓冲 |
| HTTP body 临时块 | 1KB（栈上） |
| OTA 期间总增量 | ~3KB |

配网态空闲堆 >80KB，OTA 期间安全。

### 3.10 实施步骤清单

#### 步骤 1：新建 ota_update 模块

- [ ] 新建 `main/ota_update.h`：公开 `ota_update_init()`（注册 app_main 自检）和 HTTP handler 声明
- [ ] 新建 `main/ota_update.c`：
  - `h_ota_upload`：multipart 流式接收 + esp_ota_begin/write/end + set_boot_partition
  - `ota_update_check_and_confirm`：app_main 启动时检测 PENDING_VERIFY + 10s 定时器确认
- [ ] `main/CMakeLists.txt` 添加 `ota_update.c`，REQUIRES 加 `app_update`

#### 步骤 2：配网页 UI 增加固件升级入口

- [ ] `wifi_portal.c` 的 `h_more` 页面加"固件升级"链接到 `/ota`
- [ ] 新增 `h_ota`：固件升级页（版本 + 文件选择 + 上传表单）
- [ ] 新增 `h_ota_upload`：POST 处理（委托 ota_update 模块）
- [ ] 新增 `h_ota_success` / `h_ota_fail`：结果页
- [ ] 路由表注册 `/ota`、`/ota/upload`、`/ota/success`、`/ota/fail`
- [ ] `HTTP_MAX_URI_HANDLERS` 确认够用（当前 28，+4=26，够）

#### 步骤 3：app_main 集成自检

- [ ] `app_main.c` 启动序列中调 `ota_update_check_and_confirm()`
- [ ] 自检逻辑：BLE 初始化成功 + 运行 10s 无 abort → mark_valid
- [ ] 自检失败 → mark_invalid_rollback_and_reboot

#### 步骤 4：版本号防降级

- [ ] `bikepower_config.h` 的 `FIRMWARE_VERSION` 加数值化版本号宏（如 `FIRMWARE_VERSION_NUM`）
- [ ] `ota_update.c` 上传时解析固件头版本号，拒绝 ≤ 当前

#### 步骤 5：编译 + 真机测试

- [ ] 编译通过，固件大小合理
- [ ] 烧录后 BLE 正常广播（自检通过 mark_valid）
- [ ] 配网页进入"固件升级"，上传同名固件 → 升级成功 → 重启 → 正常运行
- [ ] 上传损坏固件 → 升级失败 → 返回错误页 → 设备仍正常
- [ ] 上传坏固件（magic 错）能写但 esp_ota_end 拒绝 → 不切换分区
- [ ] 自检回滚测试：人为让新固件 abort → 重启回滚到旧固件

#### 步骤 6：文档同步

- [ ] 更新 README.md 固件升级说明
- [ ] 更新 04-technical-reference.md 增加 OTA 章节
- [ ] 更新 CHANGELOG.md
- [ ] 更新 PROGRESS.md

### 3.11 关键 API 参考

| API | 作用 |
|-----|------|
| `esp_ota_get_next_update_partition(NULL)` | 获取下一个待写入的 OTA 分区 |
| `esp_ota_begin(part, OTA_SIZE_UNKNOWN, &handle)` | 开始 OTA 写入会话 |
| `esp_ota_write(handle, data, len)` | 流式写入固件数据 |
| `esp_ota_end(handle)` | 结束写入并校验固件头 |
| `esp_ota_set_boot_partition(part)` | 设置下次启动分区 |
| `esp_ota_mark_app_valid_cancel_rollback()` | 确认当前固件有效 |
| `esp_ota_mark_app_invalid_rollback_and_reboot()` | 标记无效并回滚重启 |
| `esp_ota_get_state_partition(part, &state)` | 查询分区 OTA 状态 |

### 3.12 参考方案

- [ESP-IDF OTA 官方文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-reference/system/ota.html)
- [ESP-IDF advanced_ota 示例](https://github.com/espressif/esp-idf/tree/master/examples/system/ota/advanced_ota)
- [AI-on-the-edge-device 双分区回滚实践](https://blog.csdn.net/gitblog_00756/article/details/151303005)
- 固件头格式：ESP-IDF `esp_image_header_t`（magic 0xE9、segment_count、chip_id 等）

---

## 四、风险与缓解

| 风险 | 级别 | 缓解 |
|------|------|------|
| OTA 上传中断电 | 中 | esp_ota_abort 废弃半成品，otadata 未切换，仍启动旧固件 |
| 新固件自检逻辑误判 | 中 | 自检条件宽松（BLE 起来 + 10s 无 abort），10s 足够覆盖常见启动失败 |
| Captive Portal 部分手机不弹 | 低 | 降级：手动访问 192.168.4.1 仍可配网 |
| DNS 53 端口冲突 | 低 | 配网期独占，退出即停；ESP-IDF 无其他 DNS 服务 |
| 4MB Flash 烧录需擦除 | 低 | 首次烧录前 `esptool.py erase_flash`，后续 OTA 无需擦除 |

---

## 五、发版节奏建议

| 版本 | 内容 | 状态 |
|------|------|------|
| v3.1.4 | Captive Portal（DNS 劫持 + 探测端点）+ 4MB OTA 分区表 | ✅ 已发版 |
| v3.1.5 | OTA 升级（配网页上传 + 防降级 + 自检回滚） | ✅ 已发版，真机升级降级测试通过 |
| v3.3.0 | FTMS 扩展（训练模式参数） | 📋 规划中 |
