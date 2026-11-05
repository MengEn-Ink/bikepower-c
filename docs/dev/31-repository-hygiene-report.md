# 仓库精简与文档治理报告

## 范围与分批

本轮从 `eb87535` 开始，核对已合并分支、源码公开接口、历史文件、用户文档、配图和 Pages。保持设备行为与配置 schema 不变；当前主备桥接等候选功能仍需实物验收。

| 批次 | 工作 | 验证 |
|---|---|---|
| 分支清理 | 删除祖先关系明确的已合并本地与远端分支；保留存在独立提交的分支 | 祖先关系、工作树状态、远端引用 |
| 源码与工具 | 删除无生产调用且无公共兼容承诺的残留，统一配图分发入口 | 引用核对、相关回归、host 与必要构建 |
| 文档与配图 | README 收敛为入口；用户手册统一操作说明；归档已执行计划和历史报告；修正图示 | 文档事实、链接、资源完整性 |
| Pages | 手册与当前门户一致，清除旧截图和旧导航；保留发布版与候选功能边界 | 桌面/手机、浅色/深色、本地及公网验证 |
| 交付记录 | 记录实际提交、验证、部署与保留项 | 当前 SHA 对应 CI、Pages 与线上元数据 |

## 初步核对

- 已删除本地和远端 `feat/core-portal-power-bridge`、`feat/pages-aihero-ide-theme`、`feat/pages-light-dark-theme`、`feat/pages-web-flash`、`feat/remove-ota`、`feat/running-mode`，均经祖先关系确认合并。
- `chore/repository-hygiene` 有 1 个独立提交，`fix/protocol-stability-and-optimization` 有 20 个独立提交，patch-id 不等同主线；保留，未自动合入其历史大范围删改。
- 旧 Pages 工作树已移除，历史测试报告保存在忽略构建目录 `build/hygiene-legacy-worktree-evidence/`。
- 五组 SVG/PNG 的部分文字与当前实现不符；Pages 存在重复配图和旧门户截图。
- 文档与图片以源码、当前进度和技术参考为依据；历史设计和验证结果不重新标成当前验收通过。

## 验证与结果

| 检查 | 本轮结果 |
|---|---|
| 精简前 Host | 6178 PASS / 0 FAIL |
| 精简后 Host | 6115 PASS / 0 FAIL，减少的 63 条断言仅对应已移除模板库 |
| 桥接生产会话、扫描、主备运行时 | ASan/UBSan 通过 |
| 板型门禁 | 19 PASS / 0 FAIL |
| 双板型正式配置编译 | generic-c3 / m5stamp-c3 均通过 |

删除 `device_template.c` 的增删、去重与 JSON 实现及旧单测，保留 `device_template.h` 的设备信息结构；删除未调用的 `board_get_type()`。新镜像 app 大小以构建日志为准，未生成发行包、未烧录、未发布或打 tag。构建中保留 SDK NimBLE 的 `key_rc` 警告；旧默认配置的无效 `CCACHE_ENABLE` 行另行清理。

文档与 Pages 后续批次执行中。本节只记录实际结果，上一轮验证不用于证明本轮新改动。
