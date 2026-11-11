/*
 * test_sc_lifecycle_harness.c — Service Changed indication 生命周期【源语义简化模型】
 *
 * ⚠️ 本文件是源语义简化模型，不是逐字节抽取的真实函数。
 * 函数逻辑参考自 ESP-IDF v5.5.4 NimBLE ble_gatts.c 以下位置，但由作者手写缩减：
 *   - ble_gatts_chr_updated (ble_gatts.c:2292-2410)
 *   - ble_gatts_bonding_restored (ble_gatts.c:2716-2820)
 *   - ble_gatts_schedule_update (ble_gatts.c:2096-2135)
 *
 * 简化点：数据结构自造（非真实 struct ble_store_value_cccd / ble_hs_conn），
 * store 用内存数组而非 NVS，conn 用简化 struct，未模拟 ATT 协议栈细节。
 * 本模型 PASS 只能验证"按作者理解的源语义"逻辑自洽，
 * 不能直接推导标准栈行为或 COM205 真机根因。
 *
 * 真正完整验证应使用 esp-emulator + virtual_board_e2e.py --security 模式。
 *
 * 编译：gcc -std=c11 -Wall -Wextra -fsanitize=address,undefined -o test_sc_lifecycle test_sc_lifecycle_harness.c
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

/* ---- 简化常量（对应 NimBLE） ---- */
#define BLE_GATTS_CLT_CFG_F_NOTIFY    0x0001
#define BLE_GATTS_CLT_CFG_F_INDICATE  0x0002
#define BLE_GATTS_CLT_CFG_F_MODIFIED  0x0004
#define BLE_ATT_OP_INDICATE_REQ       0x1D
#define BLE_ATT_OP_NOTIFY_REQ          0x1B

/* SC characteristic value handle (对应 BikePower 0x2a05 Service Changed) */
#define SC_CHR_VAL_HANDLE  3
/* HRS characteristic value handle (对照) */
#define HRS_CHR_VAL_HANDLE 8

#define MAX_PEERS 4
#define MAX_CCCDS 16
#define MAX_CONNS 4

/* ---- store CCCD record (对应 struct ble_store_value_cccd) ---- */
struct store_cccd {
    bool     used;
    uint8_t  peer_addr_type;
    uint8_t  peer_addr[6];
    uint16_t chr_val_handle;
    uint16_t flags;          /* BLE_GATTS_CLT_CFG_F_* */
    uint8_t  value_changed;  /* 1 = pending indication to send */
};

/* ---- connection (对应 struct ble_hs_conn) ---- */
struct conn {
    bool     active;
    uint8_t  peer_addr[6];
    uint16_t indicate_val_handle; /* 非0 = outstanding indication */
};

/* ---- 全局状态 ---- */
static struct store_cccd s_store[MAX_CCCDS];
static struct conn       s_conns[MAX_CONNS];
static int s_indication_sent_count;   /* 发出的 INDICATE_REQ 计数 */
static int s_notify_sent_count;       /* 发出的 NOTIFY_REQ 计数 */
static int s_store_writes;           /* store 写入次数 */

/* ---- mock store 接口 (对应 ble_store_read_cccd / ble_store_write_cccd) ---- */
static int store_read_cccd_by_peer(uint8_t *peer_addr, uint16_t chr_handle,
                                   struct store_cccd *out) {
    for (int i = 0; i < MAX_CCCDS; i++) {
        if (s_store[i].used &&
            memcmp(s_store[i].peer_addr, peer_addr, 6) == 0 &&
            s_store[i].chr_val_handle == chr_handle) {
            *out = s_store[i];
            return 0;
        }
    }
    return -1;
}

static int store_write_cccd(struct store_cccd *cccd) {
    for (int i = 0; i < MAX_CCCDS; i++) {
        if (s_store[i].used &&
            memcmp(s_store[i].peer_addr, cccd->peer_addr, 6) == 0 &&
            s_store[i].chr_val_handle == cccd->chr_val_handle) {
            s_store[i] = *cccd;
            s_store_writes++;
            return 0;
        }
    }
    /* 新建记录 */
    for (int i = 0; i < MAX_CCCDS; i++) {
        if (!s_store[i].used) {
            s_store[i] = *cccd;
            s_store[i].used = true;
            s_store_writes++;
            return 0;
        }
    }
    return -1;
}

/* ---- 真实函数：ble_gatts_schedule_update (ble_gatts.c:2096-2135) ---- */
static uint8_t schedule_update(struct conn *c, uint16_t cccd_flags) {
    uint8_t att_op = 0;

    if (!(cccd_flags & BLE_GATTS_CLT_CFG_F_MODIFIED)) {
        /* 未修改，不发 */
        att_op = 0;
    } else if (cccd_flags & BLE_GATTS_CLT_CFG_F_NOTIFY) {
        /* Notify 立即发 */
        att_op = BLE_ATT_OP_NOTIFY_REQ;
    } else if (cccd_flags & BLE_GATTS_CLT_CFG_F_INDICATE) {
        /* Indicate: 每个 peer 只允许一个 outstanding indication。
         * 如果仍在等 ACK，标记更新，等 ACK 后再发；否则立即发。 */
        if (c->indicate_val_handle != 0) {
            att_op = 0;
        } else {
            att_op = BLE_ATT_OP_INDICATE_REQ;
        }
    } else {
        att_op = 0;
    }

    /* 如果要发，清 MODIFIED 标志避免重发 */
    if (att_op != 0) {
        cccd_flags &= ~BLE_GATTS_CLT_CFG_F_MODIFIED;
    }
    return att_op;
}

/* ---- 真实函数：ble_gatts_chr_updated (ble_gatts.c:2292-2410) ---- */
static void chr_updated(uint16_t chr_val_handle) {
    struct store_cccd cccd_value;
    int persist;
    int i;

    /* 遍历活动连接，对订阅 peer 发 indication */
    for (i = 0; i < MAX_CONNS; i++) {
        if (!s_conns[i].active) continue;
        /* 查该连接 peer 的 CCCD */
        if (store_read_cccd_by_peer(s_conns[i].peer_addr, chr_val_handle, &cccd_value) != 0)
            continue;
        if (!(cccd_value.flags & BLE_GATTS_CLT_CFG_F_INDICATE))
            continue;

        uint16_t flags = cccd_value.flags | BLE_GATTS_CLT_CFG_F_MODIFIED;
        uint8_t op = schedule_update(&s_conns[i], flags);
        if (op == BLE_ATT_OP_INDICATE_REQ) {
            s_conns[i].indicate_val_handle = chr_val_handle;
            s_indication_sent_count++;
        }
    }

    /* 遍历 persist CCCD（无连接时也会执行），设 value_changed=1 写 store */
    for (i = 0; i < MAX_CCCDS; i++) {
        if (!s_store[i].used) continue;
        if (s_store[i].chr_val_handle != chr_val_handle) continue;

        persist = 1; /* 假设该 CCCD 是 persist 的 */

        /* 只有当 value_changed 未被设置时才写（不覆盖相同数据） */
        if (persist && !s_store[i].value_changed) {
            s_store[i].value_changed = 1;
            store_write_cccd(&s_store[i]);
        }
    }
}

/* ---- 真实函数：ble_gatts_bonding_restored (ble_gatts.c:2716-2820) ---- */
static void bonding_restored(uint16_t conn_idx) {
    struct conn *c = &s_conns[conn_idx];
    struct store_cccd cccd_value;
    int idx;

    /* 遍历该 peer 的所有 store CCCD */
    for (idx = 0; idx < MAX_CCCDS; idx++) {
        if (!s_store[idx].used) continue;
        if (memcmp(s_store[idx].peer_addr, c->peer_addr, 6) != 0) continue;

        cccd_value = s_store[idx];

        /* 恢复 CCCD flags 到连接 */
        uint16_t clt_cfg_flags = cccd_value.flags;

        uint8_t att_op = 0;
        if (cccd_value.value_changed) {
            /* 断连期间值变了，调度发送 */
            clt_cfg_flags |= BLE_GATTS_CLT_CFG_F_MODIFIED;
            att_op = schedule_update(c, clt_cfg_flags);
        }

        switch (att_op) {
        case 0:
            break;
        case BLE_ATT_OP_NOTIFY_REQ:
            s_notify_sent_count++;
            /* Notify 发了立即清 store value_changed */
            cccd_value.value_changed = 0;
            store_write_cccd(&cccd_value);
            break;
        case BLE_ATT_OP_INDICATE_REQ:
            c->indicate_val_handle = cccd_value.chr_val_handle;
            s_indication_sent_count++;
            /* Indicate 不等 ACK 立即清 store；真实栈等 ATT ACK 后才清。
             * 这里模拟"发了但 ACK 未到"状态：store value_changed 保留。 */
            break;
        }
    }
}

/* ---- 真实行为：ATT Handle Value Confirmation ---- */
static void att_ack_indication(uint16_t conn_idx) {
    struct conn *c = &s_conns[conn_idx];
    uint16_t handle = c->indicate_val_handle;
    if (handle == 0) return;

    /* 清除 outstanding indication */
    c->indicate_val_handle = 0;

    /* 清 store 中该 CCCD 的 value_changed */
    for (int i = 0; i < MAX_CCCDS; i++) {
        if (s_store[i].used &&
            memcmp(s_store[i].peer_addr, c->peer_addr, 6) == 0 &&
            s_store[i].chr_val_handle == handle) {
            s_store[i].value_changed = 0;
            store_write_cccd(&s_store[i]);
        }
    }

    /* 如果还有其他 pending CCCD（F_MODIFIED），发下一个 */
    for (int i = 0; i < MAX_CCCDS; i++) {
        if (!s_store[i].used) continue;
        if (memcmp(s_store[i].peer_addr, c->peer_addr, 6) != 0) continue;
        if (s_store[i].chr_val_handle == handle) continue;
        if (!s_store[i].value_changed) continue;
        if (!(s_store[i].flags & BLE_GATTS_CLT_CFG_F_INDICATE)) continue;

        uint16_t flags = s_store[i].flags | BLE_GATTS_CLT_CFG_F_MODIFIED;
        uint8_t op = schedule_update(c, flags);
        if (op == BLE_ATT_OP_INDICATE_REQ) {
            c->indicate_val_handle = s_store[i].chr_val_handle;
            s_indication_sent_count++;
        }
        break;
    }
}

/* ---- 测试辅助 ---- */
static void reset(void) {
    memset(s_store, 0, sizeof(s_store));
    memset(s_conns, 0, sizeof(s_conns));
    s_indication_sent_count = 0;
    s_notify_sent_count = 0;
    s_store_writes = 0;
}

static void add_bonded_peer(uint8_t *addr, uint16_t chr_handle, uint16_t flags) {
    struct store_cccd c = {0};
    c.used = true;
    memcpy(c.peer_addr, addr, 6);
    c.chr_val_handle = chr_handle;
    c.flags = flags;
    c.value_changed = 0;
    store_write_cccd(&c);
}

static void connect_peer(int idx, uint8_t *addr) {
    s_conns[idx].active = true;
    memcpy(s_conns[idx].peer_addr, addr, 6);
    s_conns[idx].indicate_val_handle = 0;
}

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  PASS: %s\n", msg); } \
    else { printf("  FAIL: %s\n", msg); failures++; } \
} while(0)

int main(void) {
    /* 合成测试地址，非真实设备 */
    uint8_t peerA[6] = {0x00,0x11,0x22,0x33,0x44,0x55};
    uint8_t peerB[6] = {0x66,0x77,0x88,0x99,0xAA,0xBB};

    printf("=== Test 1: 无连接时 chr_updated 持久 SC pending ===\n");
    reset();
    /* peerA 已绑定并订阅 SC indication */
    add_bonded_peer(peerA, SC_CHR_VAL_HANDLE, BLE_GATTS_CLT_CFG_F_INDICATE);
    /* 无活动连接 */
    chr_updated(SC_CHR_VAL_HANDLE);
    /* 验证：store 中 peerA 的 SC CCCD value_changed=1 */
    struct store_cccd c;
    store_read_cccd_by_peer(peerA, SC_CHR_VAL_HANDLE, &c);
    CHECK(c.value_changed == 1, "无连接时 SC CCCD value_changed=1 已持久化");
    CHECK(s_indication_sent_count == 0, "无连接时无 INDICATE 发出");

    printf("\n=== Test 2: 未订阅首 peer 不消耗 SC pending ===\n");
    reset();
    add_bonded_peer(peerA, SC_CHR_VAL_HANDLE, BLE_GATTS_CLT_CFG_F_INDICATE);
    add_bonded_peer(peerB, HRS_CHR_VAL_HANDLE, BLE_GATTS_CLT_CFG_F_INDICATE);
    chr_updated(SC_CHR_VAL_HANDLE);
    /* peerB 连接（但 peerB 没订阅 SC） */
    connect_peer(0, peerB);
    bonding_restored(0);
    /* peerB 不应收到 SC indication */
    CHECK(s_indication_sent_count == 0, "未订阅 SC 的 peerB 不收到 indication");
    /* peerA 的 store value_changed 应仍为 1（未被 peerB 消耗） */
    store_read_cccd_by_peer(peerA, SC_CHR_VAL_HANDLE, &c);
    CHECK(c.value_changed == 1, "peerA 的 SC pending 未被 peerB 消耗");

    printf("\n=== Test 3: 加密恢复正确 peer 发送 SC indication ===\n");
    reset();
    add_bonded_peer(peerA, SC_CHR_VAL_HANDLE, BLE_GATTS_CLT_CFG_F_INDICATE);
    chr_updated(SC_CHR_VAL_HANDLE);
    /* peerA 加密恢复连接 */
    connect_peer(0, peerA);
    bonding_restored(0);
    CHECK(s_indication_sent_count == 1, "加密恢复时 peerA 收到 SC INDICATE");
    CHECK(s_conns[0].indicate_val_handle == SC_CHR_VAL_HANDLE, "outstanding indication 已设置");

    printf("\n=== Test 4: ATT ACK 清除 store pending ===\n");
    /* 延续 Test 3 状态 */
    att_ack_indication(0);
    CHECK(s_conns[0].indicate_val_handle == 0, "ACK 后 outstanding indication 清除");
    store_read_cccd_by_peer(peerA, SC_CHR_VAL_HANDLE, &c);
    CHECK(c.value_changed == 0, "ACK 后 store value_changed 清回 0");

    printf("\n=== Test 5: 发送失败保留 pending（无 ACK） ===\n");
    reset();
    add_bonded_peer(peerA, SC_CHR_VAL_HANDLE, BLE_GATTS_CLT_CFG_F_INDICATE);
    chr_updated(SC_CHR_VAL_HANDLE);
    connect_peer(0, peerA);
    bonding_restored(0);
    CHECK(s_indication_sent_count == 1, "INDICATE 已发送");
    /* 不 ACK，直接断开 */
    s_conns[0].active = false;
    /* 重新连接 */
    connect_peer(0, peerA);
    bonding_restored(0);
    /* 因为 store value_changed 仍为 1（未 ACK 清除），应再次发送 */
    store_read_cccd_by_peer(peerA, SC_CHR_VAL_HANDLE, &c);
    CHECK(c.value_changed == 1, "未 ACK 时 store value_changed 保留");
    CHECK(s_indication_sent_count == 2, "重连后再次发送 SC INDICATE");

    printf("\n=== Test 6: 多 CCCD 排队（一个 outstanding，等 ACK 发下一个） ===\n");
    reset();
    add_bonded_peer(peerA, SC_CHR_VAL_HANDLE, BLE_GATTS_CLT_CFG_F_INDICATE);
    add_bonded_peer(peerA, HRS_CHR_VAL_HANDLE, BLE_GATTS_CLT_CFG_F_INDICATE);
    chr_updated(SC_CHR_VAL_HANDLE);
    chr_updated(HRS_CHR_VAL_HANDLE);
    connect_peer(0, peerA);
    bonding_restored(0);
    /* 两个都 value_changed=1，但只有一个能发（indicate_val_handle != 0） */
    CHECK(s_indication_sent_count == 1, "多 CCCD 只有一个 INDICATE 发出");
    /* ACK 后应发下一个 */
    att_ack_indication(0);
    CHECK(s_indication_sent_count == 2, "ACK 后第二个 INDICATE 发出");

    printf("\n%s\n", failures == 0 ? "全部通过" : "存在失败");
    return failures;
}
