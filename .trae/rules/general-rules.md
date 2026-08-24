# BikePower C 项目规则

## 核心架构

- 技术栈：ESP-IDF v5.5.4 + NimBLE + FreeRTOS，ESP32-C3。
- `main/`：平台与设备生命周期。
- `components/bikepower_domain/`：纯 C 领域逻辑，不依赖 ESP-IDF。
- `test/`：host 单测；`tests/`：静态、mock、真机与发布门禁。
- `main/bikepower_config.h` 保存平台、协议和运行时常量；`domain_config.h` 保存领域常量。

## 强制约束

- BLE 是核心功能；普通配网保持 BLE 在线。
- WiFi 启停只允许经过 `radio_ctrl`，禁止开机自启 WiFi。
- ISR 只调用 `*FromISR` API。
- NimBLE 回调不执行 stop/deinit、NVS 写入或阻塞操作。
- 日志统一使用 `ESP_LOGI/W/E/D`，发布默认 WARN。
- 配置字段增删必须同步领域层、NVS、Portal、mock、测试和文档。
- 设备内 OTA 与家庭 WiFi 已移除，不得残留当前入口。

## 文档

- `README.md`：项目概览。
- `PROGRESS.md`：当前状态、验证和待办。
- `CHANGELOG.md`：唯一历史记录。
- `docs/user/NN-*.md`：用户说明。
- `docs/dev/NN-*.md`：当前开发规范。
- `docs/INDEX.md`：文档索引。
- 过时方案直接删除，由 Git 历史追溯；禁止新增 `docs/archive`、`docs/wiki`、`docs/superpowers`。
- 图片只保留被当前文档引用的 PNG/JPG。

## 工作流

1. 编辑前运行 `git status --short`。
2. 先写失败测试，再改实现。
3. 按变更风险运行聚焦测试、full gate 和 `idf.py build`。
4. 硬件行为改动必须真机验证；无法执行时明确记录 `NOT_RUN`。
5. 更新 `CHANGELOG.md` 和必要的当前文档。
6. 按功能分批提交；用户确认提交后立即 push。

## 发布

- 版本号、DIS、tag 与 Release 必须一致。
- 发布产物必须包含双板型 merged bin、SHA256、manifest、测试报告、用户手册、图片和总包。
- 两个板型的当前版本/Git ID/P0 证据必须全部通过。
