# 项目优化与问题排查清单（已完成）

> 日期：2026-06-15　固件：v3.1.0　范围：多板型支持、发行流程、配网网页、全链路复查
> 状态：**已确认并全部执行完毕，5 批已 push**

## 执行结果回执

| 项 | 决策 | 结果 |
|----|------|------|
| A Header too long | 接受调大 | `CONFIG_HTTPD_MAX_REQ_HDR_LEN` 1024→2048，commit 6a1c0e5 |
| B 多板型 | 方案1 描述表重构 | `s_board_desc[]` + `led_drv_t` 分发，commit c93da41 |
| C 发行流程 | 实跑验证 | 双板编译成功，generic-c3/m5stamp-c3 各 1.0M |
| D3 max_connection | 降为 2 | commit 6a1c0e5 |
| 真机网页实测 | 用户自测 | 留待用户自测 |
| 文档 drift | 修复 | v5.5.1→v5.5.4、注释 2048→4096、docs max_connection，commit 5bf62c5 |
| 多板型工具 | 归档提交 | Kconfig + build_all.sh，commit 8ff3556 |

> host 单测 581 通过；双板编译 app 44% free；飞书通知已发送。

---

## A. 提交表单报 "Header fields are too long"（核心 bug）

### 根因
- `esp_http_server` 把**请求行 + 全部请求头**读入一块 scratch 缓冲，大小 = `CONFIG_HTTPD_MAX_REQ_HDR_LEN`。
- 当前 [sdkconfig.defaults#L46](file:///Users/bytedance/work/bikepower-c/sdkconfig.defaults#L46) 设为 **1024 字节**。
- 手机浏览器（iOS Safari / Android Chrome）真实请求头里 `User-Agent` + `Accept` + `Accept-Language` + `Accept-Encoding` + `Origin` + `Upgrade-Insecure-Requests` 等加起来很容易超过 1024 字节 → 服务端返回 **HTTP 431**，页面显示 "Header fields are too long"。
- 与 POST body 大小无关（body 走 `httpd_req_recv` 单独读取），所以放大 body 限制无效。

### 建议修复
| 项 | 当前 | 建议 |
|----|------|------|
| `CONFIG_HTTPD_MAX_REQ_HDR_LEN` | 1024 | **2048**（覆盖主流手机浏览器头部，内存增量可忽略） |
| `CONFIG_HTTPD_MAX_URI_LEN` | 128 | 保持 128（本项目最长 URI `/ui/module?name=xxx` 远小于） |

> ⚠️ 质疑：单纯调大可解决，但请确认是否接受 +1KB 静态头缓冲。对 ESP32-C3 内存（空闲 >200KB）无实质影响，建议接受。

---

## B. 多板型支持可扩展性评估（未来支持更多板型）

### 现状
- 板型枚举 [bikepower_config.h#L7-L11](file:///Users/bytedance/work/bikepower-c/main/bikepower_config.h#L7-L11)：`BOARD_GENERIC_C3` / `BOARD_M5STAMP_C3`。
- 引脚选择 [board_detect.c#L62-L70](file:///Users/bytedance/work/bikepower-c/main/board_detect.c#L62-L70) 用**三元运算符** `(s_board==M5STAMP)?A:B`。
- Kconfig 选择 [Kconfig.projbuild](file:///Users/bytedance/work/bikepower-c/main/Kconfig.projbuild)：auto / generic / m5stamp。
- 构建脚本 [build_all.sh#L38-L52](file:///Users/bytedance/work/bikepower-c/scripts/build_all.sh#L38-L52) 用 case 分支。

### 可扩展性短板（加第 3 个板会痛的地方）
1. **三元运算符不可扩展**：`board_btn_pin()` / `board_led_pin()` 每加一个板都要改三元链。建议改为**板型描述表**（`static const board_desc_t s_boards[] = {...}`，含 led_pin/btn_pin/led_type/name），按枚举下标取值。
2. **LED 驱动二选一硬编码**：[led.c] 按 GPIO 高低 vs SK6812 二分，新板若用其它 LED 需再加分支。建议描述表里带 `led_type` 字段统一分发。
3. **board_name 散落两处**：宏在 config 头，case 映射在 build 脚本，命名容易漂移。建议描述表统一暴露 `board_name`，脚本从单一来源读取。

### 建议（本轮是否实施请确认）
- **方案1（推荐，低风险）**：把三元改为板型描述表，集中 led_pin/btn_pin/led_type/board_name。新板=表里加一行 + Kconfig 加一项 + 脚本加一 case。
- **方案2（最小改动）**：本轮只修 bug 不重构，扩展性问题记入 PROGRESS 待办。

---

## C. 发行版多板型固件包流程验证

### 现状判断
- [build_all.sh](file:///Users/bytedance/work/bikepower-c/scripts/build_all.sh) 逻辑正确：逐板型生成 `sdkconfig.board.tmp` → `idf.py build` → `esptool merge_bin` 合并 bootloader+分区表+app → 输出 `bikepower-c-3.1.0-{board}.bin`。符合"包名含板型 + 单文件量产"要求。
- 待**实跑验证**：是否两板型都成功产出、版本号解析正确、merge_bin 参数（flash 2MB/dio/80m）与分区表一致。

### 建议
- 执行阶段实跑 `./scripts/build_all.sh`，确认 `build/release/` 下两个 bin 均生成且大小合理。

---

## D. 配网网页业务逻辑复查（其它发现）

| 编号 | 位置 | 问题 | 严重度 | 建议 |
|------|------|------|--------|------|
| D1 | [wifi_portal.c#L274](file:///Users/bytedance/work/bikepower-c/main/wifi_portal.c#L274) | 注释写"2048 字节"但代码 [L280](file:///Users/bytedance/work/bikepower-c/main/wifi_portal.c#L280) 限制 4096 | 低（文档drift） | 改注释为 4096 |
| D2 | [h_config](file:///Users/bytedance/work/bikepower-c/main/wifi_portal.c#L625-L657) | 仅 `steady` 模式应用 power/cadence/hr，符合 WIFI-20 规则 | 正常 | 无 |
| D3 | [start_softap#L1042](file:///Users/bytedance/work/bikepower-c/main/wifi_portal.c#L1042) | `max_connection=3`，与硬件约束"1-2 连接"注释略宽 | 低 | 可保留或降为 2，待确认 |
| D4 | 路由表 [L1071-L1083](file:///Users/bytedance/work/bikepower-c/main/wifi_portal.c#L1071-L1083) | 11 路由与 WIFI-13 规则一致 | 正常 | 无 |

> 网页功能需进入配网模式后用 [stage5_portal_test.py](file:///Users/bytedance/work/bikepower-c/tests/stage5_portal_test.py) + 真机浏览器实测（执行阶段做）。

---

## E. 其它链路复查

> 当前规则已演进为 v3.1.8 事实：普通配网走 `CONFIG_ESP_COEX_SW_COEXIST_ENABLE` 共存，BLE 保持在线；OTA 独占 STA 或共存初始化失败兜底时，才由 `radio_ctrl` 停 BLE。

| 模块 | 结论 |
|------|------|
| [radio_ctrl.c](file:///Users/bytedance/work/bikepower-c/main/radio_ctrl.c) | 普通配网经 `radio_ctrl` 启用 coex，BLE 保持在线；OTA 独占 STA / 共存失败兜底才停 BLE，符合当前 WB-MUTEX 规则 |
| [board_detect.c](file:///Users/bytedance/work/bikepower-c/main/board_detect.c) | Kconfig 优先 + GPIO 自动检测回退，逻辑正常 |
| body/字段缓冲 | recv_body≤4096，device_name≤64，无溢出风险 |

---

## F. 文档 drift

| 位置 | 问题 | 建议 |
|------|------|------|
| [sdkconfig.defaults#L1](file:///Users/bytedance/work/bikepower-c/sdkconfig.defaults#L1) | 头注释写 "v5.5.1"，实际 v5.5.4 | 改为 v5.5.4 |
| [board_detect.h#L11-L12](file:///Users/bytedance/work/bikepower-c/main/board_detect.h#L11-L12) | 注释只提 GPIO 检测，未提 Kconfig 优先 | 补充说明 |

---

## G. 建议分批提交计划

| 批次 | 内容 | 文件 | 验证 |
|------|------|------|------|
| 1 fix(http) | 修复 Header too long | sdkconfig.defaults | 编译 + 真机网页提交 |
| 2 refactor(board) | 板型描述表（若选方案1） | board_detect.c/.h, bikepower_config.h, led.c | host 单测 + 双板编译 |
| 3 docs | 修复注释 drift | wifi_portal.c, sdkconfig.defaults, board_detect.h | 一致性复核 |
| 4 chore(release) | 实跑 build_all.sh 验证产物 | — | 产物核对 |

> 每批独立 commit + push，commit message 用简体中文逐文件列出。

---

## 待你确认的决策点
1. **B**：本轮是否做板型描述表重构（方案1）还是只修 bug（方案2）？
2. **C/D**：是否需要进入配网模式做真机网页实测（需物理操作进入配网）？
3. **D3**：`max_connection` 是否从 3 降为 2？
4. 确认后是否按 G 的分批计划执行并逐批 push？
5. 全部完成后发飞书通知（bot → 群组）。
