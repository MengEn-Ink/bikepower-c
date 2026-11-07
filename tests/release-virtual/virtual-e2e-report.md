# 虚拟板完整链路验证报告（production D5 源码）

## 构建

| 项 | 值 |
|----|-----|
| 入口 | `scripts/dev_workflow.ps1 build --mode production --board generic` |
| 构建目录 | `build/devcache/production` |
| 构建耗时 | 21s |
| 产物大小 | 0x119910 bytes |
| 构建退出码 | 0 |

| 项 | 值 |
|----|-----|
| 入口 | `scripts/virtual_board.ps1 build` |
| 构建目录 | `build/virtual-board-build` |
| 产物大小 | 0x119920 bytes |
| 构建退出码 | 0 |

## E2E 结果

| 项 | 结果 |
|----|------|
| 总项数 | 20 |
| PASS | 20 |
| FAIL | 0 |
| 耗时 | 40.685s |
| 固件 SHA256 | `d0826ee20e339ba67acfea9412351043db0e1aa4c4ecc2ad20982bb2da82be08` |
| 源码身份 | `5a525cf`（working tree 含 D5 未提交改动） |

### 已执行项

| 项 | 结果 | 备注 |
|----|------|------|
| flash_id | PASS | |
| write_flash | PASS | |
| verify_flash | PASS | |
| read_flash | PASS | |
| flash_exact_readback | PASS | |
| boot_identity | PASS | version=4.3.3, board=generic-c3 |
| eight_groups_read | PASS | schema=14, revision=1 |
| cycling_notifications | PASS | cps=3, csc=3, hr=3, ftms=3 |
| standard_features | PASS | |
| stale_revision | PASS | |
| begin | PASS | |
| incomplete_commit | PASS | |
| cancel | PASS | |
| invalid_value_unchanged | PASS | |
| config_commit_reconnect | PASS | |
| nvs_reset_persistence | PASS | |
| gatt_rebuild | PASS | |
| restore_original | PASS | |
| cleanup_restore_reset_verified | PASS | |
| controller_scope | PASS | ltk_checks=0 |

## 未取得（诚实报告，不虚绿）

| 项 | 状态 | 原因 |
|----|------|------|
| 安全专项绑定 | NOT_RUN | ltk_checks=0，绑定未建立 |
| SMP 加密 E2E | NOT_RUN | 绑定未建立 |
| SC 发送确认 | NOT_OBTAINED | 绑定未建立，未触发 SC indication |
| IRK 路径验证 | NOT_OBTAINED | 绑定未建立 |
| security_store_activate on_sync 重验 NVS | NOT_OBTAINED | 绑定未建立 |

## 边界

- 测试控制器请求并核对双方 LTK；不模拟射频 AES-CCM、不证明隐私地址解析
- 未执行：真实射频；实际 AES-CCM 链路加密；Windows/手机缓存；实体掉电；电气灯效；已绑定跨 GATT 表的系统缓存；目标骑跑 App；macOS 本机运行
- 安全专项：NOT_RUN
- 跑步专项：NOT_RUN

## 关键结论

1. **production D5 源码（ble_bond_cache 布局摘要 + security_store_activate + IRK 路径）在虚拟板上编译通过，20 项基础 E2E 全部 PASS**
2. **八组配置读写一致性 PASS**（schema=14, revision=1）
3. **GATT 重建 PASS**，但 SC 发送确认未测（绑定未建立）
4. **绑定/加密/SC/IRK 路径 NOT_OBTAINED**，需安全专项 E2E 单独验证
5. 虚拟板通过 ≠ COM205 真实板通过，但能快速收敛固件基础逻辑问题
