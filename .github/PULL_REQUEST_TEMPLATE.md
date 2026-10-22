## 需求与方案

- 关联 Issue / 需求：
- 一句话概要：
- 关键约束与不变量（BLE、WiFi 共存、ISR、分区或兼容性）：

## 改动与影响

- 修改内容：
- 用户可见行为：
- 固件/配置兼容性：
- 安全与凭据影响：
- 回滚方式：

## 测试与验收

| 类型 | 命令 | 实际结果 |
| --- | --- | --- |
| Quick harness | `python3 tests/operation_gate.py --level quick` |  |
| Full harness | `python3 tests/operation_gate.py --level full --build` |  |
| Diff 卫生 | `git diff --check` |  |
| 真机验收 | `python3 tests/operation_gate.py --level device ...` |  |

- [ ] 正常、异常、边界和回归路径已覆盖。
- [ ] C 代码改动已通过 `idf.py build`。
- [ ] 领域层改动已通过 host 单测。
- [ ] 设备行为改动已记录串口、BLE 与人工验收结果；未执行项保持 `NOT_RUN`。
- [ ] 用户行为、测试门禁和发版流程变更已同步文档。
