# 桥接自动恢复与多设备备选实施计划

> 归档于 2026-11-05：源码实现与自动化步骤已执行，原检查框及命令保留为历史计划，不作为执行证据。当前设计见 [主备恢复规范](../dev/28-bridge-failover-multi-device-design.md)，验证结果及未执行实物项见 [清理验证报告](../dev/30-local-cleanup-verification-report.md)。

> **For agentic workers:** REQUIRED SUB-SKILL: Use `test-driven-development` for every production change; execute this plan task-by-task with review checkpoints.

**Goal:** 实现最多三台功率计的持久化主备列表、重启自动连接、60 秒快速恢复、模拟降级和低频自动恢复，并保证真实 0 W 不参与校正。

**Architecture:** 保留 `ble_power_bridge` 为单设备 NimBLE 会话，新建纯 C `bridge_failover` 负责候选顺序和运行状态，再用 `bridge_runtime` 在应用层协调会话、扫描、NVS 最近成功提示和 Portal 命令。配置 schema 升到 v14，NVS 使用完整三设备 wire v2，BLE 配置服务保持 48 B 分组并扩展到八组。

**Tech Stack:** C11、ESP-IDF v5.5.4、NimBLE、FreeRTOS、NVS、原生 HTML/CSS/JavaScript、Python/Playwright、ASan/UBSan。

---

## 范围与批次

本需求跨越配置、BLE 会话、应用运行时和 Portal，但共享同一设备表与状态机契约，拆成多个独立计划会引入临时双数据模型。因此使用一个计划，按以下可验证批次实施：

1. 设计文档与工作树基线。
2. 三设备领域模型、wire v2 和 schema v14。
3. 八组 BLE 配置协议。
4. 纯 C 主备协调状态机。
5. 单设备会话与应用运行时适配。
6. NVS 最近成功提示、冷启动和实际输出切换。
7. Portal API、mock 和浏览器交互。
8. 文档、静态门禁、双板构建和真机验收准备。

## 文件结构

### 新建

- `components/bikepower_domain/include/bridge_failover.h`：纯 C 主备状态、事件、动作和查询接口。
- `components/bikepower_domain/bridge_failover.c`：候选排序、两轮快速恢复、模拟降级和低频轮转。
- `main/bridge_runtime.h`：应用层桥接运行时公开接口与 Portal 命令。
- `main/bridge_runtime.c`：协调 `bridge_failover`、`ble_power_bridge`、`ble_scanner` 和最近成功 NVS。
- `test/test_bridge_failover.c`：主备状态机宿主机单测。
- `test/test_bridge_runtime.c`：应用适配层离线替身测试。
- `tests/check_bridge_failover_contract.py`：冷启动、主循环、Portal mailbox 和无后台扫描静态契约。

### 修改

- `components/bikepower_domain/include/domain_config.h`：schema v14、三设备和状态机时间常量。
- `components/bikepower_domain/include/power_bridge.h`、`power_bridge.c`：设备表、NVS wire v2、旧 wire v1 迁移和 0 W 校正回归。
- `components/bikepower_domain/include/config_store.h`、`config_store.c`：v14 迁移和三设备 JSON。
- `components/bikepower_domain/include/json_miniparse.h`、`json_miniparse.c`：有界对象数组提取。
- `components/bikepower_domain/include/ble_config_codec.h`、`ble_config_codec.c`：主设备兼容组和两个备选组。
- `components/bikepower_domain/CMakeLists.txt`、`test/Makefile`：注册新领域源和测试。
- `test/test_power_bridge.c`、`test/test_domain.c`：wire、迁移、JSON、0 W 和八组协议测试。
- `main/bikepower_config.h`：两个配置特征 UUID、最近成功 NVS key。
- `main/ble_config_service.c`：注册两个备选特征。
- `main/ble_power_bridge.h`、`ble_power_bridge.c`：单候选终态，不再内部独占 60 秒重试窗口。
- `main/nvs_store.h`、`nvs_store.c`：完整 bridge blob 与最近成功运行元数据。
- `main/app_main.c`、`main/CMakeLists.txt`：运行时初始化、tick、输出选择和配置更新。
- `main/wifi_portal.h`、`wifi_portal.c`：桥接命令回调、运行状态快照和三设备 API。
- `main/web/index.html`、`app.css`、`app.js`：三设备列表、实际来源、降级倒计时和共享校正提示。
- `tests/bridge_stubs.h`、`test/test_bridge_session.c`、`tests/test_bridge_session.py`：会话与运行时 harness。
- `tests/mock_server.py`、`tests/portal_bridge_browser.py`、`tests/portal_exception_browser.py`：多设备交互和异常回归。
- `tests/test_ble_config_service.c`、`tests/test_ble_config_service.py`、`tests/ble_config_e2e.py`：八组配置协议。
- `tests/check_config_consistency.py`、`tests/check_nvs_schema.py`、`tests/operation_gate.py`、`scripts/tools/quality_gate.py`：新 schema 和门禁接线。
- `README.md`、`docs/INDEX.md`、`docs/dev/01-technical-reference.md`、`docs/wiki/01-architecture.md`、`docs/wiki/03-domain-layer.md`、`docs/wiki/08-wifi-portal.md`、`.trae/rules/general-rules.md`、`.trae/rules/domain/ble.md`、`.trae/rules/domain/wifi.md`、`CHANGELOG.md`、`PROGRESS.md`：事实同步。

---

### Task 1：隔离工作树并固化设计基线

**Files:**
- Add: `docs/dev/28-bridge-failover-multi-device-design.md`
- Add: `docs/dev/29-bridge-failover-multi-device-implementation-plan.md`

- [ ] **Step 1：创建独立工作树**

```bash
git worktree add "/Users/bytedance/work/bikepower-c-bridge-failover" \
  -b feat/bridge-failover origin/main
cp "/Users/bytedance/work/bikepower-c/docs/dev/28-bridge-failover-multi-device-design.md" \
  "/Users/bytedance/work/bikepower-c-bridge-failover/docs/dev/"
cp "/Users/bytedance/work/bikepower-c/docs/dev/29-bridge-failover-multi-device-implementation-plan.md" \
  "/Users/bytedance/work/bikepower-c-bridge-failover/docs/dev/"
git -C "/Users/bytedance/work/bikepower-c-bridge-failover" status --short
```

Expected: 仅两份新增文档；基线 HEAD 为 `origin/main`。

- [ ] **Step 2：运行文档格式检查**

```bash
git -C "/Users/bytedance/work/bikepower-c-bridge-failover" diff --check
python3 "/Users/bytedance/work/bikepower-c-bridge-failover/scripts/tools/check_markdown_links.py"
```

Expected: 零 whitespace 错误，Markdown 链接检查通过。

- [ ] **Step 3：提交并推送设计批次**

```bash
git -C "/Users/bytedance/work/bikepower-c-bridge-failover" add \
  docs/dev/28-bridge-failover-multi-device-design.md \
  docs/dev/29-bridge-failover-multi-device-implementation-plan.md
git -C "/Users/bytedance/work/bikepower-c-bridge-failover" commit -m "$(cat <<'EOF'
docs(bridge): 固化多设备自动恢复设计与实施计划

- docs/dev/28-bridge-failover-multi-device-design.md：记录三设备、模拟降级与低频恢复规范
- docs/dev/29-bridge-failover-multi-device-implementation-plan.md：拆分 TDD 实施和验收步骤
EOF
)"
git -C "/Users/bytedance/work/bikepower-c-bridge-failover" push -u origin feat/bridge-failover
```

Expected: 新分支远端存在，工作树干净。

---

### Task 2：建立三设备领域模型与 NVS wire v2

**Files:**
- Modify: `components/bikepower_domain/include/domain_config.h`
- Modify: `components/bikepower_domain/include/power_bridge.h`
- Modify: `components/bikepower_domain/power_bridge.c`
- Modify: `test/test_power_bridge.c`

- [ ] **Step 1：先写三设备、去重、旧格式迁移和 0 W 失败用例**

在 `test/test_power_bridge.c` 增加以下测试入口并从 `main()` 调用：

```c
static bridge_device_t peer(uint8_t type, uint8_t tail, const char *name)
{
    bridge_device_t item = { .bound = 1, .addr_type = type };
    item.addr[5] = tail;
    strncpy(item.name, name, sizeof(item.name) - 1);
    return item;
}

static void multi_device_config_cases(void)
{
    bridge_config_t c;
    power_bridge_config_defaults(&c);
    c.source = BRIDGE_SOURCE_REAL;
    c.device_count = 3;
    c.devices[0] = peer(0, 0x11, "Primary");
    c.devices[1] = peer(1, 0x22, "Backup");
    c.devices[2] = peer(1, 0x22, "Duplicate");
    assert(power_bridge_config_validate(&c));
    assert(c.device_count == 2);
    assert(c.devices[0].addr[5] == 0x11);
    assert(c.devices[1].addr[5] == 0x22);

    uint8_t wire[BRIDGE_WIRE_MAX_LEN];
    size_t length = power_bridge_config_encode(&c, wire, sizeof(wire));
    assert(length > 47 && wire[0] == BRIDGE_WIRE_VERSION);
    bridge_config_t restored;
    assert(power_bridge_config_decode(wire, length, &restored));
    assert(power_bridge_config_equal(&c, &restored));

    c.calibration_mode = BRIDGE_CALIBRATION_OFFSET;
    c.watts = 200;
    assert(power_bridge_correct(&c, 0) == 0);
    c.calibration_mode = BRIDGE_CALIBRATION_PERCENT;
    c.percent_x10 = 1500;
    assert(power_bridge_correct(&c, 0) == 0);
}
```

另构造现有 16+name 的 wire v1 字节，断言解码后 `device_count == 1`、设备位于槽位 0、校正参数不丢失。

- [ ] **Step 2：运行测试确认 RED**

```bash
make -C test test_power_bridge
./test/test_power_bridge
```

Expected: 编译失败于 `bridge_device_t`、`device_count` 或 `devices` 尚不存在。

- [ ] **Step 3：定义固定容量模型和常量**

在 `domain_config.h` 定义：

```c
#define CONFIG_VERSION                  14
#define BRIDGE_DEVICE_MAX               3
#define BRIDGE_WIRE_LEGACY_VERSION      1
#define BRIDGE_WIRE_VERSION             2
#define BRIDGE_WIRE_MAX_LEN             125
#define BRIDGE_FAST_WINDOW_MS           60000u
#define BRIDGE_BACKGROUND_INTERVAL_MS   60000u
#define BRIDGE_FAST_ROUNDS              2
```

在 `power_bridge.h` 将单设备字段拆出：

```c
typedef struct {
    int bound;
    int addr_type;
    uint8_t addr[6];
    char name[BRIDGE_NAME_MAX];
} bridge_device_t;

typedef struct {
    int source;
    int calibration_mode;
    int watts;
    int percent_x10;
    int device_count;
    bridge_device_t devices[BRIDGE_DEVICE_MAX];
} bridge_config_t;
```

增加：

```c
bool bridge_device_equal(const bridge_device_t *a, const bridge_device_t *b);
const bridge_device_t *power_bridge_device(const bridge_config_t *config, int slot);
int power_bridge_find_device(const bridge_config_t *config, const bridge_device_t *device);
```

- [ ] **Step 4：实现校验和 wire v2**

wire v2 使用固定头和变长设备项：

```text
[version=2][source][count][calibration][watts_le16][percent_le16]
重复 count 次：[addr_type][addr_6][name_len][name_utf8]
```

实现必须：

- 接受 wire v1 并迁移到槽位 0。
- v2 严格校验总长度和每个名称边界。
- 删除无效地址类型、全零地址、控制字符名称和重复身份。
- 保留合法条目并压紧数组。
- 设备表为空时把来源修正为模拟。
- `power_bridge_correct()` 继续先执行 `raw <= 0` 返回 0。

- [ ] **Step 5：运行领域测试确认 GREEN**

```bash
make -C test clean
make -C test run
```

Expected: `test_domain` 与 `test_power_bridge` 全部 PASS。

- [ ] **Step 6：提交并推送领域模型批次**

```bash
git add components/bikepower_domain/include/domain_config.h \
  components/bikepower_domain/include/power_bridge.h \
  components/bikepower_domain/power_bridge.c \
  test/test_power_bridge.c
git commit -m "$(cat <<'EOF'
feat(bridge): 增加三设备配置与 wire v2

- domain_config.h：升级 schema 并集中主备恢复常量
- power_bridge.h/c：定义三设备表、去重校验和 v1 到 v2 解码
- test_power_bridge.c：覆盖迁移、往返、重复设备和零功率保护
EOF
)"
git push
```

---

### Task 3：完成 schema v14 迁移与三设备 JSON

**Files:**
- Modify: `components/bikepower_domain/include/json_miniparse.h`
- Modify: `components/bikepower_domain/json_miniparse.c`
- Modify: `components/bikepower_domain/config_store.c`
- Modify: `test/test_domain.c`
- Modify: `tests/check_config_consistency.py`

- [ ] **Step 1：写 JSON 数组与 v13 迁移失败用例**

在 `test/test_domain.c` 增加：

```c
static void test_bridge_v14_json(void)
{
    config_data_t source;
    config_store_defaults(&source);
    source.bridge.source = BRIDGE_SOURCE_REAL;
    source.bridge.device_count = 3;
    source.bridge.devices[0] = make_bridge_device(0, 0x11, "Primary");
    source.bridge.devices[1] = make_bridge_device(1, 0x22, "备用功率计");
    source.bridge.devices[2] = make_bridge_device(0, 0x33, "Third");

    char json[CONFIG_JSON_MAX_LEN];
    CHECK(config_store_serialize(&source, json, sizeof(json)) > 0,
          "v14 三设备配置可序列化");
    CHECK(strstr(json, "\"bridge_devices\":[") != NULL,
          "v14 JSON 包含设备数组");

    config_data_t restored;
    CHECK(config_store_deserialize(json, &restored),
          "v14 三设备配置可反序列化");
    CHECK(power_bridge_config_equal(&source.bridge, &restored.bridge),
          "v14 三设备 JSON 往返一致");

    config_data_t v13 = source;
    v13.schema_ver = 13;
    v13.bridge.device_count = 1;
    CHECK(config_store_migrate(&v13), "v13 迁移到 v14");
    CHECK(v13.schema_ver == 14 && v13.bridge.device_count == 1,
          "v13 单设备绑定迁移后保留");
}
```

在该测试函数前增加确定性 helper：

```c
static bridge_device_t make_bridge_device(uint8_t type, uint8_t tail,
                                          const char *name)
{
    bridge_device_t device = { .bound = 1, .addr_type = type };
    device.addr[5] = tail;
    strncpy(device.name, name, sizeof(device.name) - 1);
    return device;
}
```

增加超量数组、重复地址、非法 `addr_type` 和名称超过 31 字节的拒绝测试。

- [ ] **Step 2：运行测试确认 RED**

```bash
make -C test test_domain
./test/test_domain
```

Expected: FAIL，JSON 不含 `bridge_devices` 或迁移版本仍为 13。

- [ ] **Step 3：扩展有界 JSON 对象数组解析**

在 `json_miniparse.h` 增加：

```c
int json_get_object_array_count(const char *json, const char *key);
bool json_get_object_array_item(const char *json, const char *key, int index,
                                char *out, size_t out_size);
```

实现要求：

- 处理字符串中的转义引号，不能用第一个 `}` 截断对象。
- 最大只读取调用方请求的前三项。
- 数组缺失返回 `-1`，空数组返回 `0`，语法错误返回 `-2`。
- 输出总是以 `\0` 结尾，容量不足返回 `false`。

- [ ] **Step 4：实现 v14 JSON 与迁移**

`config_store_serialize()` 输出：

```json
"bridge_devices":[
  {"addr_type":0,"address":"001122334455","name":"Primary"},
  {"addr_type":1,"address":"aabbccddeeff","name":"Backup"}
]
```

`config_store_deserialize()` 必须先解析到临时对象，再执行：

1. `schema_ver == 13` 时接受旧单设备字段并迁移到槽位 0。
2. `schema_ver == 14` 时要求 `bridge_devices` 数组合法。
3. 解析失败不修改调用方对象。
4. 最后统一调用 `config_store_migrate()` 和 `config_store_validate()`。

更新 `tests/check_config_consistency.py`，把固定 schema 断言改为 14，并要求技术参考包含 `v13→v14`。

- [ ] **Step 5：验证 GREEN 与容量**

```bash
make -C test run
python3 tests/check_config_consistency.py
```

Expected: 全部 PASS；最长三设备 JSON 小于 `CONFIG_JSON_MAX_LEN`。

- [ ] **Step 6：提交并推送 schema 批次**

```bash
git add components/bikepower_domain/include/json_miniparse.h \
  components/bikepower_domain/json_miniparse.c \
  components/bikepower_domain/config_store.c \
  test/test_domain.c tests/check_config_consistency.py
git commit -m "$(cat <<'EOF'
feat(config): 升级桥接 schema v14

- json_miniparse.h/c：增加有界对象数组读取
- config_store.c：序列化三设备并保留 v13 单设备迁移
- test_domain.c：覆盖 JSON 往返、边界和迁移
- check_config_consistency.py：更新 schema 静态契约
EOF
)"
git push
```

---

### Task 4：扩展到八组 BLE 配置协议

**Files:**
- Modify: `main/bikepower_config.h`
- Modify: `components/bikepower_domain/include/ble_config_codec.h`
- Modify: `components/bikepower_domain/ble_config_codec.c`
- Modify: `main/ble_config_service.c`
- Modify: `test/test_power_bridge.c`
- Modify: `test/test_ble_config_service.c`
- Modify: `tests/ble_config_e2e.py`

- [ ] **Step 1：先写八组和旧主组兼容测试**

测试必须断言：

```c
CHECK(BLE_CONFIG_GROUP_COUNT == 8, "配置组数量为八");
CHECK(BLE_CONFIG_GROUP_MASK_ALL == 0xff, "八组事务掩码完整");

uint8_t payload[BLE_CONFIG_GROUP_MAX_LEN];
size_t primary_len = ble_config_encode_group(BLE_CONFIG_GROUP_BRIDGE,
                                              &source, payload, sizeof(payload));
CHECK(primary_len <= 48, "主桥接组保持 MTU 64");
CHECK(ble_config_decode_group(BLE_CONFIG_GROUP_BRIDGE,
                              payload, primary_len, &decoded),
      "主桥接组可解码");
CHECK(decoded.bridge.devices[1].addr[5] == source.bridge.devices[1].addr[5],
      "只写主组时保留备选槽位");
```

对 `BLE_CONFIG_GROUP_BRIDGE_BACKUP_1` 和 `_2` 分别执行往返，并断言每组不超过 48 B。

- [ ] **Step 2：运行测试确认 RED**

```bash
make -C test test_power_bridge
./test/test_power_bridge
python3 tests/test_ble_config_service.py
```

Expected: FAIL 于新组枚举或新特征不存在。

- [ ] **Step 3：定义两个新增组与 UUID**

在 `ble_config_codec.h` 把新组追加到 Bridge 后：

```c
BLE_CONFIG_GROUP_BRIDGE,
BLE_CONFIG_GROUP_BRIDGE_BACKUP_1,
BLE_CONFIG_GROUP_BRIDGE_BACKUP_2,
BLE_CONFIG_GROUP_COUNT,
```

在 `bikepower_config.h` 分配：

```c
#define BLE_CONFIG_BRIDGE_BACKUP_1_UUID_BYTES \
    0x21, 0x0b, 0x8c, 0x4a, 0x1f, 0x6d, 0x3e, 0x9b, \
    0x2a, 0x4f, 0x7a, 0x4c, 0x0a, 0x00, 0x5a, 0x7a
#define BLE_CONFIG_BRIDGE_BACKUP_2_UUID_BYTES \
    0x21, 0x0b, 0x8c, 0x4a, 0x1f, 0x6d, 0x3e, 0x9b, \
    0x2a, 0x4f, 0x7a, 0x4c, 0x0b, 0x00, 0x5a, 0x7a
```

这两个宏分别对应 `7a5a000a-4c7a-4f2a-9b3e-6d1f4a8c0b21` 和
`7a5a000b-4c7a-4f2a-9b3e-6d1f4a8c0b21`。

- [ ] **Step 4：实现分组编码**

- 原 Bridge 组保持 wire v1 的字段顺序，映射全局校正和槽位 0。
- 备选组格式固定为 `[protocol][bound][addr_type][addr_6][name_len][name]`。
- 解码某一组只更新该槽位，保留 staging 中其他槽位。
- 单组解码只做该槽位的局部字段校验，不在组间写入过程中压紧数组。
- COMMIT 前的 `config_store_validate()` 才统一去重、压紧；删除槽位时后续设备前移。
- `BLE_CONFIG_PROTOCOL_VERSION` 保持 1，meta group mask 改为 `0xff`。

- [ ] **Step 5：注册 GATT 特征并更新 E2E**

在 `ble_config_service.c` 注册 `7a5a000a`、`7a5a000b` 两个 Read/Write 特征，均复用 `group_access`。更新服务结构测试中的 characteristic 索引，E2E 按 meta mask 动态遍历八组。

- [ ] **Step 6：运行协议回归**

```bash
make -C test run
python3 tests/test_ble_config_service.py
python3 tests/test_ble_config_e2e.py --help
python3 tests/test_ble_protocol_vectors.py
```

Expected: host 与 ASan/UBSan PASS；E2E 客户端能识别八组且不要求连接真机执行 `--help`。

- [ ] **Step 7：提交并推送配置协议批次**

```bash
git add main/bikepower_config.h \
  components/bikepower_domain/include/ble_config_codec.h \
  components/bikepower_domain/ble_config_codec.c \
  main/ble_config_service.c test/test_power_bridge.c \
  test/test_ble_config_service.c tests/ble_config_e2e.py
git commit -m "$(cat <<'EOF'
feat(ble-config): 增加两个桥接备选配置组

- bikepower_config.h：分配 000a 和 000b 配置特征 UUID
- ble_config_codec.h/c：保持主组兼容并编码两个备选槽位
- ble_config_service.c：把配置事务扩展到八组
- 桥接与配置服务测试：覆盖 48 字节上限和旧组保留行为
EOF
)"
git push
```

---

### Task 5：实现纯 C 主备协调状态机

**Files:**
- Create: `components/bikepower_domain/include/bridge_failover.h`
- Create: `components/bikepower_domain/bridge_failover.c`
- Create: `test/test_bridge_failover.c`
- Modify: `components/bikepower_domain/CMakeLists.txt`
- Modify: `test/Makefile`

- [ ] **Step 1：定义公开事件和动作**

`bridge_failover.h` 使用以下边界：

```c
typedef enum {
    BRIDGE_FAILOVER_DISABLED,
    BRIDGE_FAILOVER_FAST_RECOVERY,
    BRIDGE_FAILOVER_LIVE,
    BRIDGE_FAILOVER_FALLBACK_SIMULATION,
    BRIDGE_FAILOVER_BACKGROUND_PROBE,
    BRIDGE_FAILOVER_PAUSED,
} bridge_failover_state_t;

typedef enum {
    BRIDGE_FAILOVER_START,
    BRIDGE_FAILOVER_VALID,
    BRIDGE_FAILOVER_FAILED,
    BRIDGE_FAILOVER_DISCONNECTED,
    BRIDGE_FAILOVER_TICK,
    BRIDGE_FAILOVER_PAUSE,
    BRIDGE_FAILOVER_RESUME,
    BRIDGE_FAILOVER_RETRY_NOW,
} bridge_failover_event_t;

typedef struct {
    bool connect;
    bool stop;
    bool persist_recent;
    int slot;
} bridge_failover_action_t;

typedef struct {
    bridge_failover_state_t state;
    int order[BRIDGE_DEVICE_MAX];
    int order_count;
    int order_index;
    int round;
    int active_slot;
    int probe_cursor;
    uint32_t fast_deadline_ms;
    uint32_t next_probe_ms;
} bridge_failover_t;
```

公开函数：

```c
void bridge_failover_init(bridge_failover_t *runtime);
bridge_failover_action_t bridge_failover_handle(
    bridge_failover_t *runtime, const bridge_config_t *config,
    const bridge_device_t *recent, bridge_failover_event_t event,
    int session_slot, uint32_t now_ms);
bool bridge_failover_uses_simulation(const bridge_failover_t *runtime);
```

- [ ] **Step 2：写完整状态序列测试**

`test/test_bridge_failover.c` 至少包含：

```c
static void all_missing_falls_back_and_rotates(void)
{
    bridge_config_t config = three_peers();
    bridge_device_t recent = config.devices[1];
    bridge_failover_t runtime;
    bridge_failover_init(&runtime);

    bridge_failover_action_t action = bridge_failover_handle(
        &runtime, &config, &recent, BRIDGE_FAILOVER_START, -1, 0);
    assert(action.connect && action.slot == 1);

    const int expected[] = {2, 0, 1, 2, 0};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        action = bridge_failover_handle(&runtime, &config, &recent,
                                        BRIDGE_FAILOVER_FAILED,
                                        action.slot, (uint32_t)(i + 1) * 8000);
        assert(action.connect && action.slot == expected[i]);
    }
    action = bridge_failover_handle(&runtime, &config, &recent,
                                    BRIDGE_FAILOVER_FAILED,
                                    action.slot, 56000);
    assert(!action.connect);
    assert(runtime.state == BRIDGE_FAILOVER_FALLBACK_SIMULATION);
    assert(bridge_failover_uses_simulation(&runtime));

    action = bridge_failover_handle(&runtime, &config, &recent,
                                    BRIDGE_FAILOVER_TICK, -1, 116000);
    assert(action.connect && runtime.state == BRIDGE_FAILOVER_BACKGROUND_PROBE);
}
```

测试文件中的 `three_peers()` 必须完整定义：

```c
static bridge_config_t three_peers(void)
{
    bridge_config_t config;
    power_bridge_config_defaults(&config);
    config.source = BRIDGE_SOURCE_REAL;
    config.device_count = 3;
    for (int i = 0; i < BRIDGE_DEVICE_MAX; i++) {
        config.devices[i].bound = 1;
        config.devices[i].addr_type = i & 1;
        config.devices[i].addr[5] = (uint8_t)(0x10 + i);
        snprintf(config.devices[i].name, sizeof(config.devices[i].name),
                 "Meter-%d", i);
    }
    return config;
}
```

另覆盖：60 秒硬截止、有效 0 W 对应 `VALID`、成功后 `persist_recent` 只触发一次、断线当前设备优先、手动暂停继续模拟、立即重试、设备删除、`uint32_t` 回绕。

- [ ] **Step 3：运行测试确认 RED**

```bash
make -C test test_bridge_failover
./test/test_bridge_failover
```

Expected: 首次因新源文件或目标不存在失败。

- [ ] **Step 4：实现最小状态机**

实现规则固定为：

1. START/DISCONNECTED/RETRY_NOW 构造“最近成功或当前设备优先，其余按槽位”的去重顺序。
2. FAST 最多两轮，且 `now - start >= 60000` 时直接降级。
3. FALLBACK 到期后每次只选一个槽位进入 BACKGROUND_PROBE。
4. PROBE 失败后游标前移并设置 `next_probe_ms = now + 60000`。
5. VALID 进入 LIVE，记录 active slot；身份变化时返回 `persist_recent=true`。
6. PAUSE 返回 stop 并进入 PAUSED；RESUME 重新开始 FAST。
7. FAST 不使用模拟；FALLBACK、PROBE、PAUSED 使用模拟。

- [ ] **Step 5：注册新源和聚合测试目标**

`components/bikepower_domain/CMakeLists.txt` 增加 `"bridge_failover.c"`。
`test/Makefile` 增加：

```make
DOMAIN_SRCS += $(DOMAIN)/bridge_failover.c

all: test_domain ble_codec_cli test_power_bridge test_bridge_failover

test_bridge_failover: test_bridge_failover.c $(DOMAIN)/bridge_failover.c \
		$(DOMAIN)/power_bridge.c $(DOMAIN)/device_identity.c
	$(CC) $(CFLAGS) -o $@ $^

run: all
	./test_domain
	./test_power_bridge
	./test_bridge_failover

clean:
	rm -f test_domain test_domain.exe ble_codec_cli ble_codec_cli.exe \
		test_power_bridge test_bridge_failover
```

- [ ] **Step 6：运行状态机和领域全量测试**

```bash
make -C test clean
make -C test run
./test/test_bridge_failover
```

Expected: 全部 PASS，编译无 warning。

- [ ] **Step 7：提交并推送协调层批次**

```bash
git add components/bikepower_domain/include/bridge_failover.h \
  components/bikepower_domain/bridge_failover.c \
  components/bikepower_domain/CMakeLists.txt \
  test/test_bridge_failover.c test/Makefile
git commit -m "$(cat <<'EOF'
feat(bridge): 增加主备故障转移状态机

- bridge_failover.h/c：实现最近成功优先、快速恢复和低频轮转
- CMakeLists.txt 与 test/Makefile：注册纯 C 协调层
- test_bridge_failover.c：覆盖降级、恢复、暂停和时间回绕
EOF
)"
git push
```

---

### Task 6：收敛单设备会话并增加 bridge_runtime

**Files:**
- Modify: `main/ble_power_bridge.h`
- Modify: `main/ble_power_bridge.c`
- Create: `main/bridge_runtime.h`
- Create: `main/bridge_runtime.c`
- Create: `test/test_bridge_runtime.c`
- Modify: `test/test_bridge_session.c`
- Modify: `tests/bridge_stubs.h`
- Modify: `tests/test_bridge_session.py`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1：先写单次会话终态测试**

修改 `test/test_bridge_session.c`：

- 连接失败后状态进入 `BLE_BRIDGE_ERROR`，不会自行再次调用 `ble_gap_connect`。
- 无通知 10 秒后终止连接并进入 ERROR。
- `ble_power_bridge_request()` 接收 `bridge_device_t`。
- 旧 generation 回调不能覆盖新候选。
- 临时测试 30 秒后仍按现有行为停止。

关键断言：

```c
static void failed_connect(void)
{
    struct ble_gap_event event = {
        .type = BLE_GAP_EVENT_CONNECT,
        .connect.status = BLE_HS_ETIMEOUT,
    };
    s_gap(&event, s_arg);
}

int connects_before = s_connects;
failed_connect();
tick(9000);
tick(60000);
assert(s_connects == connects_before);
assert(ble_power_bridge_status(&s_config, 60000).state == BLE_BRIDGE_ERROR);
```

- [ ] **Step 2：写 bridge_runtime 离线测试**

`test/test_bridge_runtime.c` 用替身记录：

- 启动时向最近成功槽位发起一次连接。
- 会话 ERROR 后切换下一槽位。
- 两轮失败后 `bridge_runtime_uses_simulation()` 为 true。
- BACKGROUND_PROBE 时连接中仍返回模拟输出。
- 两份样本有效后返回真实输出并只调用一次 recent 保存。
- SCAN_START 先停止上游，再启动扫描。
- TEST 不修改持久配置和最近成功提示。

- [ ] **Step 3：运行 harness 确认 RED**

```bash
python3 tests/test_bridge_session.py
```

Expected: FAIL 于旧会话仍自重试、缺少 `bridge_runtime` 或旧 peer 类型。

- [ ] **Step 4：把 ble_power_bridge 收敛为单候选会话**

删除会话内部的 60 秒 retry window 和指数退避职责。保留：

- 8 秒连接超时。
- 服务、特征、CCCD 与 DIS 流程。
- 两份样本门槛。
- 3 秒样本过期和 10 秒无通知终止。
- temporary 测试超时。
- stop、radio stop、host reset 和 generation 隔离。

`ble_bridge_status_t.peer` 改为 `bridge_device_t`，并保证 ERROR 是稳定终态，直到收到新的 request/stop。

- [ ] **Step 5：实现 bridge_runtime 适配**

`bridge_runtime.h` 提供：

```c
typedef enum {
    BRIDGE_RUNTIME_SCAN_START,
    BRIDGE_RUNTIME_SCAN_STOP,
    BRIDGE_RUNTIME_TEST,
    BRIDGE_RUNTIME_STOP,
    BRIDGE_RUNTIME_RESUME,
    BRIDGE_RUNTIME_RETRY_NOW,
} bridge_runtime_command_t;

typedef struct {
    bridge_failover_state_t state;
    int active_slot;
    int candidate_slot;
    uint32_t next_attempt_ms;
    bool configured_real;
    bool actual_simulation;
    ble_bridge_status_t session;
} bridge_runtime_snapshot_t;

void bridge_runtime_init(const bridge_config_t *config, int sport_profile,
                         const bridge_device_t *recent, uint32_t now_ms);
void bridge_runtime_apply_config(const bridge_config_t *config, int sport_profile,
                                 uint32_t now_ms);
esp_err_t bridge_runtime_request(bridge_runtime_command_t command,
                                 const bridge_device_t *device);
void bridge_runtime_tick(const bridge_config_t *config, int sport_profile,
                         uint32_t now_ms);
bridge_runtime_snapshot_t bridge_runtime_snapshot(const bridge_config_t *calibration,
                                                  uint32_t now_ms);
bool bridge_runtime_uses_simulation(void);
```

命令使用现有 C3 临界区有界 mailbox；mailbox 满返回 `ESP_ERR_INVALID_STATE`，主任务以外不调用 NimBLE/NVS。

`bridge_runtime_tick()` 对会话状态做边沿消费：

- 新候选首次进入稳定 ERROR 时只上报一次 `BRIDGE_FAILOVER_FAILED`。
- LIVE 候选明确断开或通知超时时只上报一次 `BRIDGE_FAILOVER_DISCONNECTED`。
- 样本首次达到 `valid` 时只上报一次 `BRIDGE_FAILOVER_VALID`。
- request generation 或候选地址变化时清除上述边沿标记，旧回调不能触发新候选事件。

- [ ] **Step 6：扩展 Python harness**

在 `tests/test_bridge_session.py` 编译第三个 `bridge-runtime` 二进制，链接：

```text
test/test_bridge_runtime.c
main/bridge_runtime.c
components/bikepower_domain/bridge_failover.c
components/bikepower_domain/power_bridge.c
```

所有替身以 `-Wall -Wextra -Werror -fsanitize=address,undefined` 运行。

- [ ] **Step 7：验证 GREEN**

```bash
python3 tests/test_bridge_session.py
make -C test run
```

Expected: 会话、扫描、runtime 三个 harness 和领域测试全部 PASS。

- [ ] **Step 8：提交并推送运行时批次**

```bash
git add main/ble_power_bridge.h main/ble_power_bridge.c \
  main/bridge_runtime.h main/bridge_runtime.c main/CMakeLists.txt \
  test/test_bridge_runtime.c test/test_bridge_session.c \
  tests/bridge_stubs.h tests/test_bridge_session.py
git commit -m "$(cat <<'EOF'
feat(bridge): 分离单设备会话与主备运行时

- ble_power_bridge.h/c：输出单候选稳定终态并移除内部长窗口重试
- bridge_runtime.h/c：在主任务协调会话、扫描和主备状态机
- 会话与运行时 harness：覆盖失败切换、模拟保持和临时测试
EOF
)"
git push
```

---

### Task 7：接入 NVS 最近成功提示、冷启动与实际输出

**Files:**
- Modify: `main/bikepower_config.h`
- Modify: `main/nvs_store.h`
- Modify: `main/nvs_store.c`
- Modify: `main/app_main.c`
- Create: `tests/check_bridge_failover_contract.py`
- Modify: `tests/check_nvs_schema.py`
- Modify: `tests/operation_gate.py`

- [ ] **Step 1：先写静态契约和 NVS key 失败断言**

`tests/check_bridge_failover_contract.py` 必须检查：

```python
checks = [
    ("bridge_runtime_init(" in app_main, "冷启动初始化主备运行时"),
    ("bridge_runtime_tick(" in app_main, "主循环驱动运行时"),
    ("bridge_runtime_uses_simulation()" in app_main, "实际来源选择不等同于持久来源"),
    ("ble_scanner_start(" not in app_main, "冷启动不得后台扫描"),
    ("NVS_KEY_BRIDGE_RECENT" in nvs_store, "最近成功提示由 NVS 模块管理"),
]
```

同时更新 `tests/check_nvs_schema.py`，要求新 key 在 `bikepower_config.h` 定义并在 `nvs_store.c` 同时读写。

- [ ] **Step 2：运行契约确认 RED**

```bash
python3 tests/check_bridge_failover_contract.py
python3 tests/check_nvs_schema.py
```

Expected: FAIL 于运行时尚未接入或 recent key 缺失。

- [ ] **Step 3：实现最近成功运行元数据**

新增：

```c
#define NVS_KEY_BRIDGE_RECENT "brrecent"
```

`nvs_store.h` 提供：

```c
bool nvs_store_load_bridge_recent(bridge_device_t *device);
bool nvs_store_save_bridge_recent(const bridge_device_t *device);
```

blob 固定为 `[version=1][addr_type][addr_6]`。读取失败、全零地址或地址类型非法返回 false；保存只在 bridge_runtime 报告身份变化时调用。

- [ ] **Step 4：接入 app_main 生命周期**

启动顺序：

1. `nvs_store_load(&s_engine)`。
2. `nvs_store_load_bridge_recent(&recent)`。
3. 下游 `ble_bikepower_start()` 成功。
4. `bridge_runtime_init()`；真实骑行配置立即发出定向连接动作。

主循环：

```c
bridge_runtime_tick(&s_engine.bridge, s_engine.sport_profile, bridge_now);
bridge_runtime_snapshot_t bridge = bridge_runtime_snapshot(&s_engine.bridge, bridge_now);

if (s_engine.sport_profile == SPORT_PROFILE_RUNNING) {
    running_snapshot_t rs = running_engine_snapshot(&s_engine.running,
                                                    HR_LOOP_INTERVAL_MS);
    int running_hr = power_engine_running_heartrate(&s_engine,
                                                    rs.speed_x100_kmh);
    s.heartrate = running_hr;
    s_engine.snapshot.heartrate = running_hr;
    ble_bikepower_set_running_data(rs, (uint8_t)running_hr);
} else if (s_engine.bridge.source == BRIDGE_SOURCE_REAL
           && !bridge.actual_simulation) {
    snapshot_t out = power_engine_bridge_snapshot(&s_engine, &bridge.session.sample);
    ble_bikepower_set_bridge_data(&bridge.session.sample, (uint8_t)out.heartrate);
} else {
    snapshot_t out = power_engine_snapshot(&s_engine);
    ble_bikepower_set_data((uint16_t)out.power, (uint8_t)out.heartrate,
                           (uint16_t)out.cadence);
}
```

配置提交成功后调用 `bridge_runtime_apply_config()`；切到跑步或模拟时停止上游，切回真实骑行时进入 FAST。

- [ ] **Step 5：接入门禁并验证**

把 `tests/check_bridge_failover_contract.py` 加入 quick gate。运行：

```bash
python3 tests/check_bridge_failover_contract.py
python3 tests/check_nvs_schema.py
python3 tests/test_bridge_session.py
python3 tests/operation_gate.py --level quick \
  --report build/bridge-failover-quick.md
```

Expected: 零 FAIL；quick 报告包含新契约。

- [ ] **Step 6：提交并推送集成批次**

```bash
git add main/bikepower_config.h main/nvs_store.h main/nvs_store.c \
  main/app_main.c tests/check_bridge_failover_contract.py \
  tests/check_nvs_schema.py tests/operation_gate.py
git commit -m "$(cat <<'EOF'
feat(bridge): 接入冷启动恢复与模拟降级输出

- nvs_store.h/c：保存最近成功设备运行提示
- app_main.c：启动和主循环驱动 bridge_runtime
- 桥接静态契约与 NVS 门禁：禁止后台扫描并校验输出来源
EOF
)"
git push
```

---

### Task 8：扩展 Portal 后端与 mock 状态

**Files:**
- Modify: `main/wifi_portal.h`
- Modify: `main/wifi_portal.c`
- Modify: `main/app_main.c`
- Modify: `tests/mock_server.py`
- Modify: `tests/test_portal_config_save.py`
- Modify: `tests/portal_exception_browser.py`

- [ ] **Step 1：先写 API 失败用例**

测试覆盖：

- `/api/state.config.bridge_devices` 返回 0–3 项。
- `/api/runtime.bridge_runtime` 区分 configured/actual/state/active/candidate/next_attempt。
- 列表满时新增必须携带 `replace_slot`。
- `retry`、`resume`、`stop`、`scan start/stop` 通过 mailbox 接受。
- 只修改共享校正或既有设备顺序时，不要求功率计当前在线或重新测试。
- 新增或替换的设备必须与当前 temporary 测试会话身份一致并已收到两份样本。
- 身份复制只能来自当前通过测试的新设备；自动切换备选不得改写开发板 DIS 身份。
- 保存结果未知和 revision 冲突时三设备草稿不丢失。

mock runtime 示例：

```json
{
  "state": "fallback_simulation",
  "configured_real": true,
  "actual_source": "simulation",
  "active_slot": -1,
  "candidate_slot": 1,
  "next_attempt_ms": 42000
}
```

- [ ] **Step 2：运行测试确认 RED**

```bash
python3 tests/test_portal_config_save.py
python3 tests/portal_exception_browser.py
```

Expected: FAIL 于 `bridge_devices` 或 `bridge_runtime` 字段不存在。

- [ ] **Step 3：增加非阻塞 Portal 命令回调**

`wifi_portal.h` 增加：

```c
typedef esp_err_t (*wifi_portal_bridge_callback_t)(
    bridge_runtime_command_t command, const bridge_device_t *device);

void wifi_portal_set_bridge_callback(wifi_portal_bridge_callback_t callback);
void wifi_portal_sync_bridge(const bridge_runtime_snapshot_t *snapshot);
```

HTTP 任务只解析、校验并投递命令；主任务回调调用 `bridge_runtime_request()`。扫描开始返回 `pending:true`，GET 继续读取 `ble_scanner_serialize_results()`。

- [ ] **Step 4：扩展 state/runtime JSON**

- `/api/state` 输出三设备配置和当前会话身份。
- `/api/runtime` 输出协调层状态、实际来源、槽位和倒计时。
- `config_object()` 使用 config_store 的结构化结果，禁止手工拼接第二份不同字段名。
- 提交配置时比较 base/requested 设备身份集合：只有新增或替换身份需要匹配
  temporary tested 会话；删除、排序和共享校正修改直接进入现有 revision 事务。
- `copy_identity=true` 时必须同时满足“本次新增/替换身份”和“当前测试会话身份匹配”，
  否则拒绝请求；运行时自动接管备选永不触发身份复制。
- `WIFI_JSON_STATE_CAPACITY`、`WIFI_JSON_RUNTIME_CAPACITY` 增量通过实际序列化测试决定；禁止无证据扩大。

- [ ] **Step 5：更新同构 mock**

`tests/mock_server.py` 实现相同字段和动作：

- 三设备固定上限、替换、删除和首选排序。
- `fast_recovery`、`fallback_simulation`、`background_probe`、`live`。
- fault 注入 `all_missing`、`backup_live`、`zero`、`stale`。
- 模拟降级期间输出继续来自模拟配置。

- [ ] **Step 6：验证后端 GREEN**

```bash
python3 tests/test_portal_config_save.py
python3 tests/portal_exception_browser.py
python3 tests/test_portal_runtime.py
python3 tests/portal_faults.py
```

Expected: 全部 PASS，ASan/UBSan 无泄漏和越界。

- [ ] **Step 7：提交并推送 Portal API 批次**

```bash
git add main/wifi_portal.h main/wifi_portal.c main/app_main.c \
  tests/mock_server.py tests/test_portal_config_save.py \
  tests/portal_exception_browser.py
git commit -m "$(cat <<'EOF'
feat(portal): 暴露桥接主备和实际来源状态

- wifi_portal.h/c：通过 mailbox 提交桥接命令并发布运行快照
- app_main.c：注册 Portal 桥接命令入口
- mock 与异常测试：覆盖三设备、降级、重试和 revision
EOF
)"
git push
```

---

### Task 9：实现三设备 Portal 交互

**Files:**
- Modify: `main/web/index.html`
- Modify: `main/web/app.css`
- Modify: `main/web/app.js`
- Modify: `tests/portal_bridge_browser.py`
- Modify: `tests/check_portal_structure.py`
- Modify: `tests/check_portal_js.py`

- [ ] **Step 1：先写三视口浏览器断言**

在 320、390、1440 三种宽度覆盖：

```python
expect(page.locator("#configuredSource")).to_have_text("真实功率计")
expect(page.locator("#actualSource")).to_contain_text("模拟降级")
expect(page.locator("#bridgeDeviceList [data-device]")).to_have_count(3)
expect(page.locator("#sharedCalibrationHint")).to_have_text(
    "这组校正适用于全部已保存功率计"
)
expect(page.locator("#nextBridgeAttempt")).to_contain_text("42 秒")
```

交互覆盖：

- 添加第二、第三台设备。
- 第四台必须选择替换槽位。
- 设为首选只改变顺序。
- 删除当前设备需要确认。
- 离线时只改共享校正并保存，不要求重新测试任何既有设备。
- 新增设备勾选身份复制后，只复制该次测试候选的 DIS；备选自动接管后开发板身份不变。
- “立即重试”进入快速恢复。
- 背景尝试期间模拟读数连续。
- `backup_live` 后实际来源切为备选设备。
- `zero` 后共享偏移仍显示 0 W。
- 运动模式未保存时继续隐藏旧实时区。

- [ ] **Step 2：运行浏览器测试确认 RED**

```bash
python3 tests/portal_bridge_browser.py
```

Expected: FAIL 于设备列表或实际来源元素不存在。

- [ ] **Step 3：增加稳定 DOM**

`index.html` 增加：

- `configuredSource`
- `actualSource`
- `bridgeDeviceList`
- `bridgeDeviceTemplate`
- `nextBridgeAttempt`
- `retryBridgeBtn`
- `sharedCalibrationHint`
- `fallbackSimulationSummary`
- 替换确认区

列表使用原生按钮和已有状态 chip，不嵌套卡片；所有操作触控高度至少 44 px。

- [ ] **Step 4：更新前端状态与编解码**

`app.js`：

- `decode()` 把 `bridge_devices` 映射到稳定设备 key。
- `encode()` 输出最多三项，不再从临时 `devices` 对象推导单一 peer。
- 保存前要求新增设备已测试；已有备选不要求重复测试。
- `renderLive()` 分开显示配置来源和实际来源。
- FALLBACK/PROBE 使用模拟输出字段，LIVE 使用真实桥接字段。
- 0 W 预览先判断 `state.rawPower <= 0`，再执行校正。
- 运行轮询只刷新 runtime，不覆盖 draft 列表。

- [ ] **Step 5：实现响应式列表样式**

保持既有明亮、高对比度主题：

- 桌面设备行使用名称、地址、状态、操作四列。
- 手机改为两行布局，操作按钮不横向溢出。
- 倒计时使用等宽数字，刷新不改变布局宽度。
- 不使用仅靠颜色表达的状态。

- [ ] **Step 6：运行 Portal 全量门禁**

```bash
python3 tests/portal_bridge_browser.py \
  --screenshot-dir build/bridge-failover-screenshots
python3 tests/check_portal_structure.py
python3 tests/check_portal_js.py
python3 tests/check_portal_render.py
python3 tests/portal_exception_browser.py
python3 scripts/inline_static.py --web-dir main/web \
  --output /tmp/bikepower-bridge-failover-web.h --report
```

Expected:

- 三视口无水平滚动、重叠和 `pageerror`。
- 压缩内联资源小于 112,640 B。
- 浅色主题下状态、禁用按钮和错误文案可读。

- [ ] **Step 7：提交并推送前端批次**

```bash
git add main/web/index.html main/web/app.css main/web/app.js \
  tests/portal_bridge_browser.py tests/check_portal_structure.py \
  tests/check_portal_js.py
git commit -m "$(cat <<'EOF'
feat(portal): 增加三设备主备管理交互

- index.html/app.css：展示设备列表、实际来源和降级恢复状态
- app.js：编辑三设备、替换、重试并保持零功率语义
- Portal 浏览器与静态测试：覆盖三视口和异常恢复
EOF
)"
git push
```

---

### Task 10：同步规则文档与最终自动化门禁

**Files:**
- Modify: `README.md`
- Modify: `docs/INDEX.md`
- Modify: `docs/dev/01-technical-reference.md`
- Modify: `docs/wiki/01-architecture.md`
- Modify: `docs/wiki/03-domain-layer.md`
- Modify: `docs/wiki/08-wifi-portal.md`
- Modify: `.trae/rules/general-rules.md`
- Modify: `.trae/rules/domain/ble.md`
- Modify: `.trae/rules/domain/wifi.md`
- Modify: `CHANGELOG.md`
- Modify: `PROGRESS.md`
- Modify: `scripts/tools/quality_gate.py`

- [ ] **Step 1：同步事实文档**

必须记录：

- `CONFIG_VERSION = 14`。
- v13→v14 和 bridge wire v1→v2 迁移。
- 三设备固定容量与“地址类型 + 地址”身份。
- 最近成功优先、60 秒快速恢复、每 60 秒轮转一台。
- 降级只改变运行时实际来源。
- 000a/000b 配置特征和八组事务。
- 0 W 不参与共享校正。
- 开机只定向连接，禁止后台扫描。

- [ ] **Step 2：更新规则**

`.trae/rules/domain/ble.md` 增加：

- 已保存来源可开机定向连接。
- 自动恢复不得扫描。
- 两份合法 0 W 样本可确认来源。
- 多设备切换不得改变共享校正。

`.trae/rules/domain/wifi.md` 更新 schema、数组字段、runtime 状态和 `retry` 动作。

- [ ] **Step 3：接入完整质量门禁**

把新静态契约加入 `scripts/tools/quality_gate.py`，并确保 quick/full 均通过 `tests/operation_gate.py` 间接执行。

- [ ] **Step 4：运行专项与 full 门禁**

```bash
make -C test clean
make -C test run
python3 tests/test_bridge_session.py
python3 tests/test_ble_config_service.py
python3 tests/test_ble_protocol_vectors.py
python3 tests/check_bridge_failover_contract.py
python3 tests/check_config_consistency.py
python3 tests/check_nvs_schema.py
python3 tests/operation_gate.py --level full --build \
  --report build/bridge-failover-operation-full.md
python3 scripts/tools/quality_gate.py --no-build
git diff --check
```

Expected: 零 FAIL；若存在 dirty WARN，只能来自本计划尚未提交的预期文件。

- [ ] **Step 5：执行双板构建**

```bash
./scripts/build_all.sh
```

Expected:

- generic-c3 和 m5stamp-c3 均构建成功。
- 合并 bin 和 SHA256 生成成功。
- app 分区余量、Portal 压缩容量和最低堆静态预算未退化。
- 本步骤只验证构建，不创建 Release、不打 tag。

- [ ] **Step 6：记录人工验收边界**

在 `PROGRESS.md` 明确以下项目未有实物证据时必须为 NOT_RUN：

- 两台以上真实或可控 CPS 来源的主备接管。
- 重启后最近成功设备自动连接。
- 全部离线后模拟连续输出。
- 设备恢复后低频自动切回。
- 真实热点、下游 App、上游功率计三方共存堆大于 80 KiB。
- 三种校正下真实 0 W。

- [ ] **Step 7：提交并推送文档与门禁批次**

```bash
git add README.md docs/INDEX.md docs/dev/01-technical-reference.md \
  docs/wiki/01-architecture.md docs/wiki/03-domain-layer.md \
  docs/wiki/08-wifi-portal.md .trae/rules/general-rules.md \
  .trae/rules/domain/ble.md .trae/rules/domain/wifi.md \
  CHANGELOG.md PROGRESS.md scripts/tools/quality_gate.py
git commit -m "$(cat <<'EOF'
docs(bridge): 同步多设备恢复规则与验证边界

- 技术参考与 Wiki：记录 schema v14、八组协议和主备状态机
- BLE/WiFi 规则：约束定向连接、模拟降级与零功率语义
- CHANGELOG/PROGRESS：记录自动验证和剩余真机边界
- quality_gate.py：接入桥接主备静态契约
EOF
)"
git push
```

- [ ] **Step 8：最终核对**

```bash
git status --short
git log --oneline origin/main..HEAD
git diff --name-status origin/main...HEAD
```

Expected: 工作树干净；提交按设计、领域配置、状态机运行时、Portal API、Portal 前端、文档门禁分批排列；没有固件发布提交。

---

## 真机验收计划

自动实现完成后再使用 `real-device-test` skill，至少准备两台真实功率计或可控 CPS 夹具。执行顺序：

1. 保存三台设备，其中一台为最近成功。
2. 断电重启，确认不扫描且先定向连接最近成功设备。
3. 关闭当前来源，确认 60 秒内尝试备选并接管。
4. 关闭全部来源，确认快速窗口后模拟功率、踏频和心率连续。
5. 恢复一台来源，确认低频尝试命中后两份样本再切回真实。
6. 向当前真实来源发送 0 W，分别验证原值、正偏移和 150% 均输出 0 W。
7. 开启真实热点和下游骑行 App，记录当前/最低空闲堆、连接稳定性和通知失败数。
8. 恢复测试前配置，并确认 NVS 重启后保持一致。

没有完成上述实物步骤前，不发布固件、不打 tag，不把多设备桥接标记为完整验收通过。
