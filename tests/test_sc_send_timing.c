/*
 * test_sc_send_timing.c — 【反证脚本，非生产 harness，不作为修复证据】
 *
 * 本脚本是纯 mock 时序模型，经 QC 审核发现重大遗漏：
 *   - 未模拟 NimBLE ble_gatts.c:2292 ble_gatts_chr_updated 遍历 persist CCCD 写 value_changed=1
 *   - 未模拟 ble_gap.c:9416 加密恢复时 ble_gatts_bonding_restored 自动重发 pending indication
 *   - 未模拟 ble_gatts.c:2788 注释所述"恢复 CCCD 并发送 pending indication"标准路径
 *
 * 因此"on_sync 无连接 = SC 永不发"的结论不成立。真实栈在加密恢复时会自动
 * 从 store 加载 value_changed=1 的 CCCD 并发送 pending indication。
 *
 * 本脚本保留为反证用例，提醒后续不要用纯 mock 替代 production harness。
 * 真正的 SC 生命周期验证需在 vm 上链接真实 NimBLE host 源码，覆盖：
 *   1. 无连接时 ble_gatts_chr_updated 把 SC CCCD value_changed=1 持久化到 store
 *   2. 未订阅首 peer 连接时不能消耗 pending SC indication
 *   3. 加密恢复（bonding_restored）正确 peer 时发送 SC indication
 *   4. ACK（Handle Value Confirmation）后 value_changed 清回 0
 *   5. 发送失败时 value_changed 保留，待下次连接重试
 */
#include <stdio.h>
int main(void) {
    printf("本脚本为反证用例，不代表 production SC 生命周期。\n");
    printf("真实 harness 需链接 NimBLE ble_gatts.c / ble_gap.c 源码，见 PR draft 说明。\n");
    return 0;
}
