# 开发前检查清单（C / ESP-IDF 版）

<!-- stability: tier-3 (动态，随开发实践变更) -->
<!-- rule-id-prefix: PDC (引用格式: PDC-N) -->
> 每次修改代码前，AI 必须先回答以下问题，确认影响范围后再编码。
> 常量命名以 `main/bikepower_config.h` 与 `components/bikepower_domain/include/domain_config.h` 为准。

---

## 通用检查

1. 本次修改影响哪些模块？（对照 general-rules.md 模块依赖图）
2. 是否需要更新 `bikepower_config.h` 或 `domain_config.h` 常量？
3. 是否触及领域层（components/bikepower_domain）？若是，host 单测（test/test_domain）是否需要补充。
4. 是否需要更新 PROGRESS.md / CHANGELOG.md / README.md？

## BLE 链路检查（NimBLE）

5. 是否影响 BLE 广播/通知链路？
6. BLE 通知回调 / GATT 回调中是否分配大缓冲或做阻塞操作？（必须预分配，回调内不重分配）
7. 是否影响 MTU、广播间隔（`BLE_ADVERTISING_INTERVAL_MS`）、通知间隔（`BLE_NOTIFY_INTERVAL_MS`）、UUID 等参数？这些必须从 config 头文件读取。
8. BLE/NimBLE 初始化失败时是否走可控恢复（记录日志 + 必要时 `esp_restart()`）？

## WiFi 链路检查（radio_ctrl / wifi_portal）

9. 是否影响 WiFi 配网页面链路？
10. `esp_wifi_start()` 前是否确认 BLE 已完全停止（停广播、断连接、停 host）？唯一 RF 开关入口是 `radio_ctrl.c`。
11. socket / HTTP 连接是否在出错路径也释放？并发是否保持精简？
12. 配网超时（`WIFI_SHUTDOWN_MS`）后是否 `esp_restart()` 恢复 BLE？
13. 是否引入了开机自动启动 WiFi？（禁止，必须长按 + 二次确认触发）

## 按钮链路检查（button.c）

14. 是否影响按钮事件处理链路（app_queue → app_state）？
15. 按钮时长阈值是否与 config 头文件一致（`SHORT_PRESS_MS` / `WIFI_BTN_HOLD_MS` / `CONFIRM_WINDOW_MS` / `BTN_AUTO_REPEAT_MS`）？
16. ISR 内是否只用 `*FromISR` API？去抖/计时/自动重复是否都在 button_task（任务上下文）完成？

## 配置持久化检查（nvs_store / config_store）

17. 是否影响 NVS 保存/恢复链路？
18. 新增配置项是否在 config 头文件、领域层校验（profile.c / power_engine.c）、配网表单三处同步？
19. NVS schema 是否变化？若是，是否有迁移方案（`CONFIG_VERSION`）？
20. 重启后配置是否能正确恢复？

## 内存与中断检查

21. 是否存在堆耗尽风险（大缓冲未预分配、socket 未释放、循环中重分配）？
22. NimBLE host 启动后空闲堆是否仍 > 80KB？（低于需告警）
23. 任务栈大小是否经实测确认，未随意缩小？
24. 主循环及长任务是否覆盖 Task WDT 喂狗（`esp_task_wdt_reset()`，`WDT_TIMEOUT_MS`）？

## 文档同步检查

25. README.md / PROGRESS.md 是否需要更新？（参数值、按钮行为、文件结构、阶段进度）
26. `.trae/rules/` 是否需要更新？（新增规则、文件结构变更）
27. CHANGELOG.md 是否需要记录本次变更？
28. 公开接口（.h）变更时，对应头文件接口注释是否同步？
