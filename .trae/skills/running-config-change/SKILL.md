---
name: running-config-change
description: BikePower C 跑步模式或配置字段变更流程。
---

# 跑步与配置字段变更

## 当前边界

- 跑步核心为速度/配速、步频、心率。
- 已支持 RSC、FTMS Treadmill、目标速度、开始、暂停、恢复和重置。
- 坡度已删除，不得恢复残留。
- 运行/暂停是会话态，不持久化。

## 修改清单

1. `domain_config.h`、`config_store.*`、相关 engine。
2. `bikepower_config.h`、`nvs_store.c`。
3. `wifi_portal.c` 与 `main/web/*`。
4. `tests/mock_server.py`。
5. `test/test_domain.c` 与相关静态门禁。
6. `CHANGELOG.md`、`PROGRESS.md`、当前用户/技术文档。

删除字段时先加残留检查并确认 RED，再删除实现；旧 JSON 带删除字段时应忽略该字段并保留其他有效配置。

## 验证

```bash
make -C test run
python3 tests/test_ble_protocol_vectors.py
python3 tests/test_portal_config_save.py
python3 tests/check_config_consistency.py
python3 tests/check_portal_structure.py
python3 scripts/tools/quality_gate.py
idf.py build
```

BLE profile 或控制行为变化时追加真机 running control 自测。
