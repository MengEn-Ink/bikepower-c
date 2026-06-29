# MicroPython vs C 方案对比与未来规划

> 🗄️ **历史 / 归档文档，仅供追溯**：本文为技术栈选型阶段的对比记录，不代表 v3.1.8 当前实现。

> 日期：2026-06-12
> 对比对象：esp32-power（MicroPython v2.3.0）vs bikepower-c（C/ESP-IDF v3.0.0）
> 硬件平台：ESP32-C3（RISC-V 160MHz, 400KB SRAM, 2MB Flash）

---

## 一、固件体积对比

| 指标 | MicroPython | C | 差异 |
|------|:-----------:|:-:|:----:|
| 固件 bin | 1,800,688 B (1.72MB) | 1,039,280 B (0.99MB) | **C 小 42%** |
| Bootloader | ~21KB | 21KB | 相当 |
| App 分区 | 1.75MB (0x1c0000) | 1.75MB (0x1c0000) | 相同 |
| Flash 剩余 | ~30KB | ~760KB | **C 多 25 倍** |
| ELF text | — | 867KB | — |
| ELF data | — | 185KB | — |
| ELF bss | — | 908KB | — |
| 源码行数 | 3,959 行 (.py) | 3,094 行 (.c/.h) | C 少 22% |

> MicroPython 固件含完整解释器 + 标准库，体积基线大。C 版只编译用到的代码，紧凑得多。

---

## 二、运行时堆内存对比

| 场景 | MicroPython | C | 差异 |
|------|:-----------:|:-:|:----:|
| BLE 启动后空闲堆 | ~132 KB | **177 KB** | **C 多 45KB (+34%)** |
| WiFi AP 启动后 | ~90 KB | ~120-140 KB（预估） | **C 多 30-50KB** |
| 告警阈值 | 40 KB (gc.mem_free) | 40 KB (HEAP_WARN_BYTES) | 相同 |
| 严重阈值 | 无 | 20 KB (HEAP_CRIT_BYTES) | C 有显式阈值 |
| 最低水位 | 无记录 | esp_get_minimum_free_heap_size() | C 可追踪 |

---

## 三、内存模型对比

| 维度 | MicroPython | C |
|------|:-----------:|:-:|
| GC 机制 | 标记-清除，全堆扫描，暂停 5-15ms | 无 GC，手动 malloc/free |
| GC 暂停影响 | BLE 通知可能延迟 5-15ms | 无暂停 |
| 内存碎片 | GC 碎片化，大对象分配可能失败 | 无碎片（预分配策略） |
| HTML 页面构建 | 每请求拼接 str，产生 3-5KB 临时对象 | SEND 宏直接写 socket，零临时分配 |
| WiFi 延迟导入 | 需手动延迟 import 节省 2-3KB | 编译时只链接用到的符号 |
| BLE 通知缓冲 | Python str 对象，每次通知创建新对象 | 预分配 static uint8_t buf |
| 配置存储 | JSON 文件读写，临时 dict 对象 | NVS 键值对，零临时分配 |
| 心率/功率计算 | Python float 运算，产生临时对象 | C 栈变量，零堆分配 |

---

## 四、架构对比

| 维度 | MicroPython | C |
|------|:-----------:|:-:|
| 框架 | MicroPython v1.24+ | ESP-IDF v5.5.4 |
| BLE 栈 | MicroPython bluetooth (NimBLE) | 原生 NimBLE |
| RTOS | _thread（受限） | FreeRTOS（完整 Queue/信号量/任务） |
| HTTP 服务 | 手写 socket + 分块 | esp_http_server |
| 配置持久化 | JSON 文件 (power_config.json) | NVS 键值对 |
| 状态机 | if/elif 内联 | 显式 7 态枚举 + handle_event |
| 领域层 | 与板级混合 | 独立 components/，纯 C 可 host 单测 |
| 按钮处理 | 主循环轮询 | GPIO 中断 → 信号量 → 独立任务 |
| LED 控制 | 主循环内联 | 独立任务 + 模式切换 |
| WiFi/BLE 互斥 | 手动检查连接数 | radio_ctrl 统一入口 |

---

## 五、稳定性对比

| 维度 | MicroPython | C |
|------|:-----------:|:-:|
| BLE 通知时序 | GC 暂停可能导致 5-15ms 抖动 | 稳定 1s 间隔 |
| 异常恢复 | try/except + machine.reset() | esp_restart() + coredump |
| 看门狗 | WDT feed 在主循环 | Task WDT + 显式喂狗 |
| Coredump | 无 | 64KB 分区，崩溃可回溯 |
| 内存监控 | gc.mem_free() 手动检查 | 10s 心跳自动检查 + 告警 |
| BLE 降级 | 初始化失败降级运行 | 初始化失败 → esp_restart() |
| WiFi 线程崩溃 | 静默，需等 180s 超时 | httpd 框架内置异常处理 |

---

## 六、综合评分

| 指标 | MicroPython | C | 胜出 |
|------|:-----------:|:-:|:----:|
| 固件体积 | 1.72MB | 0.99MB | **C (-42%)** |
| BLE 空闲堆 | 132KB | 177KB | **C (+34%)** |
| GC 暂停 | 5-15ms | 0ms | **C** |
| 内存碎片 | 有 | 无 | **C** |
| Flash 剩余 | ~30KB | ~760KB | **C (25x)** |
| 开发效率 | 高（REPL/热更新） | 低（编译-烧录循环） | MicroPython |
| 运行时稳定性 | 中 | 高 | **C** |
| BLE 通知时序 | 有抖动 | 稳定 | **C** |
| 可调试性 | REPL 在线调试 | 串口日志 + coredump | 各有千秋 |
| OTA 更新 | 替换 .py 文件 | 需实现 OTA 分区 | MicroPython |

---

## 七、未来功能扩展规划

### 7.1 Flash 空间预算（当前剩余 ~760KB）

| 规划功能 | 预估 Flash 占用 | 预估 RAM 占用 | 优先级 |
|---------|:--------------:|:------------:|:-----:|
| OTA 分区（双 app） | +1.75MB（需改分区表） | ~4KB | P1 |
| BLE 扫描（STA 配网） | +8KB | +6KB | P2 |
| 自定义 WiFi AP 密码 | +2KB | +1KB | P3 |
| 固件版本回退 | +4KB | +2KB | P2 |
| 功率数据记录 | +12KB | +8KB | P3 |

> 当前 2MB Flash 单分区方案无法支持 OTA 双分区。需升级 4MB Flash 模组或改用压缩 OTA 方案。

### 7.2 RAM 空间预算（当前 BLE 空闲 177KB）

| 规划功能 | 预估 RAM 占用 | 剩余堆 | 优先级 |
|---------|:------------:|:-----:|:-----:|
| BLE 扫描（STA 配网） | +15KB | ~162KB | P2 |
| 功率历史缓冲（最近 60s） | +2KB | ~175KB | P3 |
| 多连接优化（3+ 设备） | +4KB | ~173KB | P3 |
| 自定义 AP 密码 | +0.5KB | ~177KB | P3 |

> 177KB 空闲堆远超 40KB 告警线，功能扩展空间充裕。

### 7.3 分阶段规划

#### P1：发版与 OTA（v3.1.0）

- **72h 老化测试**：当前固件连续运行 72h，确认无 coredump、无内存泄漏
- **打 tag v3.0.0**：老化通过后正式发版
- **OTA 分区方案调研**：
  - 方案 A：升级 4MB Flash 模组，双 OTA 分区（推荐，业界标准）
  - 方案 B：2MB Flash + 压缩 OTA（差分升级，实现复杂）
  - 方案 C：2MB Flash + 最小化 OTA 分区（缩减 app 分区至 1.2MB，当前 0.99MB 够用但余量小）

#### P2：STA 配网（v3.2.0）

- **BLE 扫描附近 WiFi**：通过 BLE 通知发送 SSID 列表到手机 App
- **STA 模式连接**：用户选择 WiFi 后设备切换 STA 连接路由
- **RF 互斥处理**：WiFi STA 与 BLE 共存需评估 RF 时分策略
- **预估内存**：STA 模式额外占用 ~15KB RAM，177KB 空闲堆足够

#### P3：增强功能（v3.3.0+）

- **自定义 WiFi AP 密码**：配网页面增加密码输入，NVS 存储
- **功率历史记录**：最近 60s 功率/心率/踏频数据环形缓冲
- **多连接优化**：支持 3+ 设备同时连接，独立通知节流
- **固件版本回退**：OTA 失败自动回退上一版本
- **XSS 防护**：配网页面输出转义（当前配网模式短暂开放，风险可控）

### 7.4 硬件升级建议

| 方案 | 成本 | 收益 | 建议 |
|------|:----:|------|------|
| 2MB → 4MB Flash | +¥1-2 | OTA 双分区 + 更多功能空间 | **推荐** |
| ESP32-C3 → ESP32-C6 | +¥3-5 | WiFi 6 + BLE 5.0 + 更大 RAM | 非必需 |
| 外部 PSRAM | 不支持 | ESP32-C3 无 PSRAM 接口 | 不可行 |

---

## 八、结论

C 版在内存、体积、稳定性三个维度全面优于 MicroPython 版本：

1. **固件小 42%**（0.99MB vs 1.72MB），Flash 剩余空间大 25 倍
2. **空闲堆多 34%**（177KB vs 132KB），无 GC 暂停和内存碎片
3. **BLE 通知时序稳定**，无 5-15ms GC 抖动
4. **架构更清晰**：领域层独立可测、状态机显式、WiFi/BLE 互斥统一入口
5. **功能扩展空间充裕**：177KB 空闲堆远超告警线，760KB Flash 剩余可支持多项增强

MicroPython 的唯一优势是开发效率和 REPL 调试便利性，但在量产固件场景下，C 版的稳定性和资源效率更具决定性。
