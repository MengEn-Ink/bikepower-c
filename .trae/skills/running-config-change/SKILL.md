---
name: running-config-change
description: BikePower C 跑步模式或配置字段变更流程。用于新增/删除/重命名跑步参数、NVS/API/网页保存字段、mock 状态字段，或调整跑步 RSC/心率闭环。
---

# 跑步模式 / 配置字段变更 Skill

用于处理容易跨层漂移的字段变更，尤其是跑步模式、网页保存、NVS schema、mock 回显。

## 先判定意图

- 只要用户目标是让跑步模式“可用”，核心闭环优先级是：配速/速度 → 步频 → 心率。
- 坡度、FTMS Treadmill Data、FTMS Control Point 属于增强兼容，不是当前 RSC 跑步 P0 的必要条件。
- 历史规划文档可以保留未来 FTMS 说明；当前功能删减只改运行时、网页、测试和当前文档。

## 修改清单

字段新增、删除或重命名时逐层检查：

1. 领域层：`components/bikepower_domain/include/domain_config.h`、`config_store.c/.h`、相关 engine 结构和 setter。
2. NVS：`main/bikepower_config.h`、`main/nvs_store.c`。
3. HTTP API：`main/wifi_portal.c` 的保存逻辑和 `/api/state` 输出。
4. 前端：`main/web/index.html`、`main/web/app.js`。
5. Mock：`tests/mock_server.py`。
6. 回归：`test/test_domain.c`、`tests/test_portal_config_save.py`、`tests/check_config_consistency.py`、`tests/check_portal_structure.py`。
7. 记录：`CHANGELOG.md`；如用户文档行为变化，再同步 `docs/user/` 和技术参考。

## 删除字段的推荐流程

1. 先改静态门禁，让旧字段/旧 DOM 残留导致失败。
2. 运行对应门禁确认 RED，例如 `python3 tests/check_portal_structure.py`。
3. 删除实现层字段和 UI。
4. host 单测补兼容：旧 JSON 带已删除字段时 deserialize 成功，其他有效字段仍生效。
5. 跑全量相关验证。

## 最低验证

字段变更至少执行：

```bash
make -C test
./test/build/test_domain
python3 tests/check_portal_structure.py
python3 tests/test_portal_config_save.py
python3 tests/check_config_consistency.py
python3 tests/check_portal_js.py
python3 scripts/tools/quality_gate.py
. /Users/bytedance/esp/esp-idf-v5.5.4/export.sh >/dev/null 2>&1 && idf.py build
```

如果改 BLE profile、RSC 打包或服务表切换，再补 `tests/ble_selftest.py --profile running`；无法真机执行时在提交说明里写明原因。
