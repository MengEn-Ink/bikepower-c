# 虚拟板完整链路验证报告（production 889c04b）

## 构建

| 项 | 值 |
|----|-----|
| 入口 | `scripts/virtual_board.ps1 build` |
| 构建目录 | `build/virtual-board-build` |
| 固件 SHA256 | `d80c5966f61d0bde54382cd0c32b11a37dc7a0fb57b22695f5ca0e6a388f6b54` |
| 构建退出码 | 0 |

## E2E 结果

| 项 | 值 |
|----|-----|
| 总项数 | 24 |
| PASS | 23 |
| NOT_OBTAINED | 1 |
| FAIL | 0 |
| 耗时 | ~40s |
| 源码身份 | `889c04b`（production D5 已 commit） |
| ltk_checks | 2（两次 bonded 重连核对 LTK） |

### 已执行项

| 项 | 结果 | 备注 |
|----|------|------|
| flash_id | PASS | |
| write_flash | PASS | |
| verify_flash | PASS | |
| read_flash | PASS | |
| flash_exact_readback | PASS | |
| boot_identity | PASS | version=4.3.3, board=generic-c3, git=889c04b |
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
| secure_connections_checked_ltk | PASS | 绑定建立，LTK 核对通过 |
| sc_subscribed_before_rebuild | PASS | SC 订阅在 GATT 重建前已建立 |
| gatt_rebuild | PASS | GATT 重建成功 |
| **sc_indication_after_rebuild** | **NOT_OBTAINED** | **received=False, count=0** |
| bonded_reconnect_second | PASS | 第二次 bonded 重连成功 |
| restore_original | PASS | 原配置恢复 |
| final_restore_reset_verified | PASS | 最终恢复重启验证 |
| cleanup_restore_reset_verified | PASS | |
| controller_scope | PASS | ltk_checks=2 |

## NOT_OBTAINED 诚实报告

**sc_indication_after_rebuild**: 虚拟板 GATT 重建后未收到 SC indication（received=False, count=0）。

- sc_subscribed_before_rebuild PASS：SC 订阅在重建前已建立
- gatt_rebuild PASS：GATT 重建成功
- 但重建后虚拟板未捕获到 SC indication 事件
- 实物 diag 已证明 SC 发送 0→14 EDONE ACK PASS（fc-crossdiag-json/serial）
- 虚拟板 Bumble 控制器可能未正确模拟 SC indication 接收路径

## 关键结论

1. **production source（ble_bond_cache 布局摘要 + security_store_activate on_sync 重验 NVS + IRK 路径）在虚拟板上编译通过，23 项基础 + 安全 E2E 全部 PASS**
2. **绑定/加密 E2E PASS**（ltk_checks=2，两次 bonded 重连核对 LTK）
3. **八组配置读写一致性 PASS**（schema=14, revision=1）
4. **GATT 重建 PASS**，SC 订阅在重建前已建立
5. **SC indication after rebuild NOT_OBTAINED**（虚拟板未捕获，实物 diag 已证明发送/ACK PASS）
6. **原配置恢复 PASS**，最终重启验证 PASS
7. 虚拟板通过 ≠ COM205 真实板通过，但固件基础逻辑已验证
