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
| 抓 BLE 空中协议 | 嗅探 BLE 通知包 | 固件保护**无效**（协议在空气中明文） |

> 关键认知：固件加密只防固件被**复制 / 反编译 / 篡改**，不防 BLE 空中协议被嗅探。后者属于协议层，与固件保护无关。

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
配置位于 [`sdkconfig.defaults`](../sdkconfig.defaults)。

| 配置项 | 作用 | 状态 |
|--------|------|------|
| `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT=y` | 去除 assert 中的 `__FILE__` / 行号明文字符串，逆向看不到源文件路径，同时减小体积 | ✅ |
| `CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y`（`=0`） | 关闭 bootloader 启动日志（分区 / 版本等信息） | ✅ |
| `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` | `-Os` 体积优化，符号 / 内联裁剪，间接降低可读性 | ✅（既有） |
| 分发 `.bin` 不含符号表 | ESP-IDF 默认 app `.bin` 不带符号（符号在 `.elf`），逆向只能裸反汇编 | ✅（IDF 默认） |

### 局限（务必知晓）

- 编译层加固**只抬高门槛，挡不住有 esptool 的人**：任意人接串口都能 `read_flash` 把固件 dump 出来并反汇编。
- 字符串常量（如配网页 HTML、日志文本）在 `.bin` 中仍可见。
- 要真正做到"读出来是密文"，必须上硬件层 Flash 加密（方案 A/B）。

---

## 四、方案 A：Flash 加密（⬜ 未实现，⚠️ 不可逆）

防提取的核心。ESP32-C3 用 AES-256(XTS) 加密 flash，密钥存 eFuse，CPU 之外无法读取。

```
CONFIG_SECURE_FLASH_ENC_ENABLED=y
CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y
```

| 模式 | 说明 | 适用 |
|------|------|------|
| Development | 可重复烧录调试，eFuse 未锁死，**可被降级攻破** | 仅开发期 |
| Release | eFuse 锁死，无法再经串口读明文，**不可逆** | 量产 |

### 对本项目的冲突点（需改造）

1. **不可逆**：Release 烧死 eFuse 后，该芯片永远无法读 flash、无法刷未签名固件，烧错即废。
2. **coredump 失效**：当前 `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` 写 flash，加密后取不到明文栈，破坏现有串口排障流程。
3. **OTA / 配网重刷**：后续固件需用同一密钥重新打包，`scripts/build_all.sh` 双板型合并流程要加加密步骤。
4. **量产烧录复杂化**：不能再用普通 `idf.py flash`，需走 `espefuse` + 预加密镜像流程。

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

> 当前结论：BikePower C 为 BLE 功率计模拟器，核心资产是功率 / 心率算法。已落地方案 C 足以抬高抄袭门槛；如转商业量产防盗版，再单独评估方案 A/B 的不可逆改造。

---

## 七、参考

- [ESP32-C3 Flash Encryption](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/security/flash-encryption.html)
- [ESP32-C3 Secure Boot v2](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/security/secure-boot-v2.html)
- [ESP-IDF Security Features](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/security/security.html)
