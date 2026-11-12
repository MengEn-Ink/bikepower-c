# 固件防提取 / 防反编译方案

> 面向 BikePower C（ESP32-C3 + ESP-IDF v5.5.4）。
> 本文整理固件保护的完整方案，并标注当前实现状态。
> 状态图例：✅ 已实现 ｜ ⬜ 未实现（可选）｜ ⚠️ 不可逆 / 需评估

---

## 一、威胁模型

| 攻击目标 | 手段 | 对应防护 |
|----------|------|----------|
| 提取固件后反编译抄代码 | `esptool read_flash` dump flash | Flash 加密 |
| 刷入篡改 / 恶意固件 | 串口重刷未授权固件 | Secure Boot v2 |
| 绕过保护 dump RAM / 重进下载 | JTAG 调试、ROM 下载模式 | 关闭 JTAG / 下载模式 |
| 降低逆向可读性 | 静态反汇编 `.bin` | 编译层加固（本项目当前方案） |
| 抓 BLE 空中协议 | 分析无线链路数据 | Flash 加密不保护无线链路；是否明文取决于连接加密状态 |

> 关键认知：Flash 加密主要提供存储保密性，Secure Boot 校验启动镜像真实性；两者不能替代 BLE 链路加密。CPS/HRS 等具体连接是否加密，应按该连接的安全状态判断，不能一概写成空中明文。编译裁剪不是安全边界，本项目源码已按 MIT 开源。

---

## 二、防护层级总览

| 层级 | 机制 | 防什么 | 可逆 | 本项目状态 |
|------|------|--------|------|-----------|
| 编译层 | Silent assert / 关 bootloader 日志 / strip 符号 | 抬高反编译门槛 | ✅ 可逆 | ✅ 已实现 |
| 硬件层 | Flash Encryption（Release） | 固件被读出明文 | ⚠️ 不可逆 | ⬜ 未实现 |
| 硬件层 | Secure Boot v2 | 刷入未授权固件 | ⚠️ 不可逆 | ⬜ 未实现 |
| 硬件层 | 关闭 JTAG / ROM 下载模式 | 调试口 dump、串口重刷 | ⚠️ 不可逆 | ⬜ 未实现 |

---

## 三、方案 C：编译层加固（✅ 已实现）

零硬件风险、完全可逆，不烧 eFuse、不影响串口排障与 coredump 流程。
配置位于 [`sdkconfig.defaults`](../../sdkconfig.defaults)。

| 配置项 | 作用 | 状态 |
|--------|------|------|
| `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT=y` | 去除 assert 中的 `__FILE__` / 行号明文字符串，逆向看不到源文件路径，同时减小体积 | ✅ |
| `CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y`（`=0`） | 关闭 bootloader 启动日志（分区 / 版本等信息） | ✅ |
| `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` | `-Os` 体积优化，符号 / 内联裁剪，间接降低可读性 | ✅（既有） |
| 分发 `.bin` 不含符号表 | ESP-IDF 默认 app `.bin` 不带符号（符号在 `.elf`），逆向只能裸反汇编 | ✅（IDF 默认） |

### 局限（务必知晓）

- 编译层加固**只抬高门槛，挡不住有 esptool 的人**：任意人接串口都能 `read_flash` 把固件 dump 出来并反汇编。
- 字符串常量（如配网页 HTML、日志文本）在 `.bin` 中仍可见。
- 当前 `CONFIG_ESP_COREDUMP_STACK_SIZE=1024`；官方 v5.5.4 指南建议独立栈大于 1300 字节。它是待验证的崩溃取证可靠性风险，不代表已复现崩溃；调整到 1536/2048 前须量化常驻内存并在隔离镜像中触发 panic、验证完整 dump 与符号化，不能直接改生产配置冒称已验收。
- 要真正做到"读出来是密文"，必须上硬件层 Flash 加密（方案 A/B）。

---

## 四、方案 A：Flash 加密（⬜ 未实现，⚠️ 不可逆）

防提取的核心。ESP32-C3 使用 XTS-AES-128，采用 256 位总密钥并保存在受保护 eFuse 中；不能误称 AES-256-XTS。见 [IDF v5.5.4 Flash 加密说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32c3/security/flash-encryption.html)。

```
CONFIG_SECURE_FLASH_ENC_ENABLED=y
CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y
```

| 模式 | 说明 | 适用 |
|------|------|------|
| Development | 可重复烧录调试，eFuse 未锁死，**可被降级攻破** | 仅开发期 |
| Release | eFuse 锁死，无法再经串口读明文，**不可逆** | 量产 |

### 对本项目的冲突点（需改造）

1. **不可逆配置**：eFuse 与下载通道限制需要单独设计。Flash 加密限制明文更新；拒绝未签名固件由 Secure Boot 实现，不能混为同一功能。
2. **coredump 路径需改造**：加密分区不能用原串口原始 Flash 读取流程直接分析，但不是必然失效。官方支持设备侧解密读取后交给分析工具；需验证授权、分区和完整排障链路，见 [Core Dump 指南](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32c3/api-guides/core_dump.html)。
3. **网页刷写冲突**：本项目设备内 OTA 已移除；当前通用 merged-bin 网页刷写不能直接套用加密量产流程。密钥与镜像分发需另行设计。
4. **量产流程需验证**：按设备密钥生成方式、下载模式与是否启用 Secure Boot 选择流程；本项目不自动烧 eFuse、关闭调试口或改变现有升级通道。

---

## 五、方案 B：Secure Boot v2（⬜ 未实现，⚠️ 不可逆）

防篡改 / 防刷机。基于 RSA-3072 签名，bootloader 校验固件签名，签名不符拒绝启动。

```
CONFIG_SECURE_BOOT=y
CONFIG_SECURE_BOOT_V2_ENABLED=y
CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y
```

- 私钥留在发布方手中，他人无法伪造可启动固件。
- 新增负担：私钥安全管理（泄露即防护失效）。
- 通常与方案 A 配合：加密防"读出"，Secure Boot 防"刷入"。

### 关闭调试 / 下载通道（配合 A/B）

```
CONFIG_SECURE_DISABLE_ROM_DL_MODE=y   # 永久禁用 ROM 下载模式（⚠️ 烧 eFuse 不可逆）
```

- 关 ROM 下载模式可挡"串口重进下载模式 dump RAM"，是软件配置里防护最强的一项，但已属不可逆 eFuse 操作。

---

## 六、选型建议

| 需求 | 推荐方案 |
|------|----------|
| 只想不被轻易抄代码、不动硬件 | **方案 C（当前已实现）** |
| 真要量产、怕固件被复制反编译 | 方案 C + 方案 A（Flash 加密 Release） |
| 量产 + 怕被刷恶意固件 | 方案 C + A + B（全开） |

> 当前结论：BikePower C 源码已按 MIT 开源；编译层裁剪用于体积与诊断策略，不作为防抄袭或完整安全保证。优先建设发布来源可验证、依赖与许可证清单、配置写权限和排障隐私边界；量产 Flash 加密/Secure Boot 需独立评审，本轮不实施。

---

## 七、参考

- [ESP32-C3 Flash Encryption](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/security/flash-encryption.html)
- [ESP32-C3 Secure Boot v2](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/security/secure-boot-v2.html)
- [ESP-IDF Security Features](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/security/security.html)
