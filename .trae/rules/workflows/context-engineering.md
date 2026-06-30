# 上下文工程规则（BikePower C）

<!-- stability: tier-2 (稳定，随治理结构变更) -->
<!-- rule-id-prefix: CE -->

> 目标：让 AI 助手按稳定规则优先、动态事实后置、按任务只读必要上下文的顺序工作。

## 加载顺序

1. `CE-1`：先读 `AGENTS.md`，只拿到 Hard Rules、验证要求和文档索引。
2. `CE-2`：再读 `.trae/memory/project-context.md`，获取用户偏好、已验证架构决策、踩坑和提交要求。
3. `CE-3`：再读通用规则：
- `.trae/rules/general-rules.md`
- `.trae/rules/coding-style.md`
- `.trae/rules/hardware-constraints.md`
- `.trae/rules/wifi-ble-mutex.md`
4. `CE-4`：按任务加载领域规则：
- 改 BLE 或 BLE 自测，读 `.trae/rules/domain/ble.md`
- 改配网、radio 或门户自测，读 `.trae/rules/domain/wifi.md`
- 改测试流程、提交流程或治理流程，读 `.trae/rules/workflows/*.md`
5. `CE-5`：再读与任务直接相关的技术文档，优先 `PROGRESS.md`、`docs/dev/01-technical-reference.md` 和对应实现文件。
6. `CE-6`：最后结合当前 `git status --short`、打开文件和用户请求，确定本次允许修改的范围。

## 任务路由建议

- 改 `main/ble_bikepower.c`、`app_main.c`、`tests/ble_selftest.py`：
先读 `domain/ble.md`，再读 `docs/dev/01-technical-reference.md` 第四章和测试脚本。
- 改 `main/wifi_portal.c`、`main/radio_ctrl.c`、`tests/stage5_portal_test.py`：
先读 `domain/wifi.md` 和 `wifi-ble-mutex.md`，再读技术参考第八章。
- 改 `components/bikepower_domain/`：
先读 `coding-style.md`、`general-rules.md`，再读领域层相关实现和 `test/test_domain.c`。
- 改治理文件：
先读 `.trae/README.md`、相关 workflow 规则，再看被引用的实际文件是否存在。

## 稳定内容优先级

- `CE-7`：稳定规则优先于一次性任务说明。
- `CE-8`：硬件硬约束优先于通用框架最佳实践。
- `CE-9`：当前仓库中的真实实现优先于旧项目记忆或外部模板。

## 禁止事项

- `CE-10`：禁止一开始就全量阅读 `.trae/`、`docs/`、`tests/` 所有文件。
- `CE-11`：禁止在未确认修改范围前直接编辑代码或规则。
- `CE-12`：禁止把探索过程或未验证猜测写入长期记忆。
- `CE-13`：禁止忽略 `project-context.md` 中的用户工作方式要求和提交规范。

## 验证

- `CE-14`：开始编辑前至少完成一次 `git status --short`。
- `CE-15`：新增治理文档时，要反查被引用路径是否真实存在。
- `CE-16`：若文档中写了数字、路由、服务数、测试脚本名，必须从当前仓库验证后再落盘。
