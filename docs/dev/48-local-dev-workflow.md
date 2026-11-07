# 48 - 本机开发验证统一入口（dev_workflow）

> 定位：本机 doctor / build / test 的统一命令行入口，**只加速本机验证，不替代项目门禁、不碰设备**。
> 固件烧录、真机 BLE 验收、正式发版构建仍由对应执行者按既有流程执行。

## 文件

| 文件 | 说明 |
|------|------|
| `scripts/tools/dev_workflow.py` | 主程序（纯标准库，Python ≥3.8） |
| `scripts/dev_workflow.ps1` | Windows 薄包装，透传参数 |
| `scripts/tools/dev_workflow.config.example.json` | 示例配置（无真实路径，可提交） |
| `tests/test_dev_workflow.py` | 自测（临时 fixtures，不真编固件） |

## 快速开始

```powershell
# 1. 机器本地配置（只在本机用户目录，绝不提交）
#    复制 scripts/tools/dev_workflow.config.example.json
#    到 %USERPROFILE%\.bikepower\dev_workflow.json 并改路径

# 2. 环境自检
.\scripts\dev_workflow.ps1 doctor

# 3. 生产模式增量构建（缓存 build/devcache/production）
.\scripts\dev_workflow.ps1 build

# 4. 维护/诊断模式（缓存与生产完全隔离）
.\scripts\dev_workflow.ps1 build --mode diagnostic --dry-run
.\scripts\dev_workflow.ps1 build --mode maintenance

# 5. 状态与清理
.\scripts\dev_workflow.ps1 status
.\scripts\dev_workflow.ps1 clean --mode diagnostic   # 只删 build/devcache/diagnostic
```

## 设计要点

- **进程环境**：IDF_PATH / IDF_PYTHON_ENV_PATH / 工具链只写进子进程 env dict，不改系统 PATH、不污染调用 shell。**工具链按 IDF `tools/tools.json` 的 `status=recommended` 版本精确选定**（target=esp32c3），不再整目录扫描新老版本混排；doctor 直接打印 build 实际使用的 riscv32-esp-elf-gcc 路径与 `--version`，与 PATH 前置段一一对应。
- **两档增量策略**：
  - **配置桶变化**（mode/board/合并 defaults/工具链版本/首次/上次失败）→ 清空缓存内全部旧产物全量重编。
  - **仅源码内容变化** → 把变化文件 copyfile 进隔离 `source-snapshot/`（新 mtime，不 touch 用户源码），未变文件快照保持旧 mtime 绝不触碰；构建以 `-C source-snapshot` 为项目根，Ninja 凭 mtime 只重编受影响 object。无快照/manifest 时按不可验证 provenance 走全量。
- **模式隔离（两道防线）**：物理目录隔离 + `mode.tag` 审计；production **显式拒绝**合并 defaults 中任何 `CONFIG_*DIAG*=y` 标记（不只 tag）。`defaults.audit.txt` 落盘实际合并全文可审计。
- **fence 输入**：内容 SHA256（不看 mtime）覆盖 main/components 源、web、嵌套 CMakeLists、Kconfig、sdkconfig*、partitions；managed 组件只 hash `idf_component.yml`/`dependencies.lock` 清单，不 hash 整 tree。
- **文件锁**：每模式一把 `build.lock`（Windows `msvcrt` / Unix `fcntl`），锁竞争等待超时给清晰错误而非静默损坏。
- **分阶段状态**：`state.json` 记录 RUNNING / PASS / FAIL / TIMEOUT、阶段、退出码、fence、defaults sha；日志在 `build/devcache/logs/`，UTF-8 可复用。
- **超时**：doctor 90s / build 2400s / test 1200s（可配置），超时杀进程树并记 TIMEOUT（读取线程轮询，真超时可靠）。

## 边界（重要）

- 本工具**不烧录、不打开串口、不碰 BLE/设备**。
- `build` 只记录 idf.py 退出码；**脚本成功 ≠ 项目门禁 PASS**。正式门禁仍以 `scripts/tools/quality_gate.py`、`tests/operation_gate.py` 与设备验收证据为准。
- `clean` 只允许删除 `build/devcache/` 下本工具自己的目录，绝不动旧 `build/` 历史产物、源码与用户文件。
- 机器本地配置在用户目录；仓库内示例配置不含任何绝对路径或密钥。

## 自测

```powershell
python tests\test_dev_workflow.py
```

覆盖（23 项）：错误 Python、路径含空格、缺依赖、内容 hash 漂移（旧 mtime 证明不靠 mtime）、同内容不漂移（快照 mtime 边界）、仅源码变化走增量（snapshot_changed 列表、不清缓存）、配置桶变化（切板型）清旧 object、三模式缓存隔离与 mode.tag 跨模式拒绝、**production 拒 DIAG=y 标记**、WSL gcc 真编译（OLD→NEW）、**WSL make 增量 fixture**（第二次无变化 "Nothing to be done"、msg.o mtime 不变、旧 mtime 改内容→快照新 mtime 重编 NEW）、锁竞争、真超时、失败不记 PASS、UTF-8、clean 只删 `build/devcache/`、进程 env 不泄漏。
