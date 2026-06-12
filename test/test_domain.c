/**
 * 领域层 host 单元测试（纯 C，clang/gcc 直接编译运行）
 * 验证 power_engine / ride_modes / hr_model / profile 的数学行为。
 * 编译: make -C test  或  clang -I../components/.../include ...
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "domain_config.h"
#include "domain_rng.h"
#include "profile.h"
#include "hr_model.h"
#include "power_engine.h"
#include "config_store.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

static void test_profile(void)
{
    printf("[test] profile 范围校验\n");
    profile_t p;
    profile_init(&p);
    CHECK(p.weight == PROFILE_WEIGHT_DEFAULT, "默认体重");
    CHECK(p.ftp == PROFILE_FTP_DEFAULT, "默认FTP");
    CHECK(p.thr_hr == PROFILE_THR_HR_DEFAULT, "默认阈值心率");
    CHECK(p.ride_type == 0, "默认配速机器人 coco");

    CHECK(!profile_set_weight(&p, 39), "体重下限拒绝");
    CHECK(!profile_set_weight(&p, 151), "体重上限拒绝");
    CHECK(profile_set_weight(&p, 75), "体重合法设置");
    CHECK(p.weight == 75, "体重已更新");
    CHECK(!profile_set_weight(&p, 75), "体重未变化返回false");

    CHECK(!profile_set_ftp(&p, 49), "FTP下限拒绝");
    CHECK(profile_set_ftp(&p, 250), "FTP合法");
    CHECK(profile_set_ride_type_str(&p, "genie"), "配速机器人字符串设置");
    CHECK(p.ride_type == 3, "genie 索引=3");
    CHECK(!profile_set_ride_type_str(&p, "unknown"), "未知配速机器人拒绝");
    CHECK(profile_ride_type_index("constance") == 4, "constance 索引=4");
}

static void test_ftp_zone(void)
{
    printf("[test] FTP 区间映射 (ftp=300 thr=190)\n");
    hr_state_t st;
    hr_model_init(&st, 150);
    // pct=0 → base 42% → 79.8 → 79
    int hr0 = hr_model_ftp_zone(&st, 0, 300, 190);
    CHECK(hr0 == hr_clamp(190 * 42 / 100), "0W 心率=恢复区基线");
    // pct=100 (300W) → 94 + 10*6/15 = 94+4=98% → 186
    int hr100 = hr_model_ftp_zone(&st, 300, 300, 190);
    CHECK(hr100 == hr_clamp(190 * 98 / 100), "FTP功率→98%THR");
    // 高功率钳制到 HR_MAX
    int hrhi = hr_model_ftp_zone(&st, 2000, 300, 190);
    CHECK(hrhi == HR_MAX, "超高功率钳制到200");
    // ftp=0 回退线性
    int hrz = hr_model_ftp_zone(&st, 100, 0, 190);
    CHECK(hrz == hr_clamp((int)(0.45f * 100 + 45)), "ftp=0 线性回退");
}

static void test_hr_smooth(void)
{
    printf("[test] 一阶指数平滑（滞后特性）\n");
    hr_state_t st;
    hr_model_init(&st, 100);
    st.hr_float_init = false;
    // 第一次调用以 target 初始化
    int first = hr_model_smooth(&st, 160);
    CHECK(first == 160, "首次平滑=target");
    // 上升追赶 (tau=30) 慢于一步到位
    hr_model_init(&st, 100);
    int prev = 100;
    for (int i = 0; i < 5; i++) {
        int v = hr_model_smooth(&st, 160);
        CHECK(v >= prev, "上升单调不减");
        CHECK(v < 160, "上升过程未越过目标");
        prev = v;
    }
    // 下降比上升慢：相同步数下降幅度更小
    hr_state_t up, down;
    hr_model_init(&up, 100);
    hr_model_init(&down, 200);
    float du = 0, dd = 0;
    for (int i = 0; i < 20; i++) {
        int u = hr_model_smooth(&up, 150);
        int d = hr_model_smooth(&down, 150);
        du = 150 - u;
        dd = d - 150;
        (void)du; (void)dd;
    }
    int u_remain = 150 - (int)up.hr_float;
    int d_remain = (int)down.hr_float - 150;
    CHECK(d_remain >= u_remain, "下降残差>=上升残差(tau_fall>tau_rise)");
}

static void test_emit_deadband(void)
{
    printf("[test] 对外台阶保持限速\n");
    hr_state_t st;
    hr_model_init(&st, 150);
    int first = hr_model_emit(&st, 150);
    CHECK(first == 150, "首次emit=raw");
    // 死区内不变（HR_EMIT_DEADBAND=2）
    int v = hr_model_emit(&st, 152);
    CHECK(v == 150, "死区内沿用旧值(+2)");
    // 超死区每次最多 +1
    v = hr_model_emit(&st, 160);
    CHECK(v == 151, "超死区每步最多+1");
    v = hr_model_emit(&st, 160);
    CHECK(v == 152, "继续步进+1");
}

static void test_adjust_power(void)
{
    printf("[test] 调功仅 steady 生效\n");
    power_engine_t e;
    power_engine_init(&e);
    CHECK(e.mode == RIDE_MODE_STEADY, "默认steady");
    int p = power_engine_adjust_power(&e, -20);
    CHECK(p == DEFAULT_POWER - 20, "steady 调功-20");
    CHECK(power_engine_is_dirty(&e), "调功置dirty");
    power_engine_clear_dirty(&e);

    // 切到 road 后调功被拒
    power_engine_set_mode(&e, RIDE_MODE_ROAD);
    power_engine_clear_dirty(&e);
    int p2 = power_engine_adjust_power(&e, -20);
    CHECK(p2 == e.base_power, "road 模式调功拒绝");
    CHECK(!power_engine_is_dirty(&e), "拒绝不置dirty");

    // 下限钳制
    power_engine_set_mode(&e, RIDE_MODE_STEADY);
    e.base_power = 10;
    int p3 = power_engine_adjust_power(&e, -50);
    CHECK(p3 == POWER_MIN, "调功钳制到下限0");
}

static void test_erg(void)
{
    printf("[test] ERG 目标功率自动切模式\n");
    power_engine_t e;
    power_engine_init(&e);
    power_engine_set_erg_target(&e, 250);
    CHECK(e.mode == RIDE_MODE_ERG, "ERG下发自动切ERG");
    CHECK(e.has_erg_target && e.erg_active_target == 250, "ERG目标=250");
    CHECK(e.base_power == 250, "base同步=250");
    // 超范围拒绝
    power_engine_set_erg_target(&e, 9999);
    CHECK(e.erg_active_target == 250, "超范围ERG拒绝");
    // ERG 快照功率围绕目标小幅波动
    domain_rng_seed(42);
    for (int i = 0; i < 50; i++) {
        snapshot_t s = power_engine_snapshot(&e);
        CHECK(s.power >= 250 + STEADY_POWER_FLUCT_LO && s.power <= 250 + STEADY_POWER_FLUCT_HI,
              "ERG功率在目标±5内");
        CHECK(s.mode == RIDE_MODE_ERG, "快照模式=ERG");
    }
}

static void test_road_pacer(void)
{
    printf("[test] road 配速功率范围\n");
    power_engine_t e;
    power_engine_init(&e);
    profile_set_weight(&e.profile, 70);
    profile_set_ride_type(&e.profile, 0); // coco 24-28
    power_engine_set_mode(&e, RIDE_MODE_ROAD);
    domain_rng_seed(7);
    int lo = 70 * 24 / 10; // 168
    int hi = 70 * 28 / 10; // 196
    for (int i = 0; i < 100; i++) {
        snapshot_t s = power_engine_snapshot(&e);
        CHECK(s.power >= lo && s.power <= hi, "coco 功率在功体比范围内");
    }
}

static void test_steady_snapshot(void)
{
    printf("[test] steady 快照功率波动范围\n");
    power_engine_t e;
    power_engine_init(&e);
    e.base_power = 200;
    domain_rng_seed(99);
    for (int i = 0; i < 100; i++) {
        snapshot_t s = power_engine_snapshot(&e);
        CHECK(s.power >= 200 + STEADY_POWER_FLUCT_LO && s.power <= 200 + STEADY_POWER_FLUCT_HI,
              "steady 功率在base±5内");
        CHECK(s.heartrate >= HR_MIN && s.heartrate <= HR_MAX, "心率合法范围");
        CHECK(s.cadence >= CADENCE_MIN && s.cadence <= CADENCE_MAX, "踏频合法范围");
    }
}

static void test_config_store(void)
{
    printf("[test] 配置 schema 默认/校验/迁移\n");
    config_data_t c;
    config_store_defaults(&c);
    CHECK(c.schema_ver == CONFIG_VERSION, "默认schema版本");
    CHECK(c.power == DEFAULT_POWER, "默认功率");
    CHECK(c.cadence == DEFAULT_CADENCE, "默认踏频");
    CHECK(c.mode == DEFAULT_RIDE_MODE, "默认模式");
    CHECK(c.tune_road == MODE_TUNE_DEFAULT, "默认road调节");
    CHECK(c.weight == PROFILE_WEIGHT_DEFAULT, "默认体重");
    CHECK(c.ftp == PROFILE_FTP_DEFAULT, "默认FTP");
    CHECK(c.thr_hr == PROFILE_THR_HR_DEFAULT, "默认阈值心率");
    CHECK(c.ride_type == PROFILE_RIDE_TYPE_DEFAULT, "默认配速机器人");
    CHECK(strcmp(c.device_name, DEVICE_NAME_DEFAULT) == 0, "默认设备名");

    // 合法数据不修正
    CHECK(!config_store_validate(&c), "默认值无需校验修正");

    // 空设备名回退默认
    config_data_t d;
    config_store_defaults(&d);
    d.device_name[0] = '\0';
    CHECK(config_store_validate(&d), "空设备名被修正");
    CHECK(strcmp(d.device_name, DEVICE_NAME_DEFAULT) == 0, "空设备名回退默认");

    // 自定义设备名保留
    config_data_t n;
    config_store_defaults(&n);
    strcpy(n.device_name, "MyBike");
    CHECK(!config_store_validate(&n), "自定义设备名合法不修正");
    CHECK(strcmp(n.device_name, "MyBike") == 0, "自定义设备名保留");

    // 越界字段夹紧
    config_data_t b;
    config_store_defaults(&b);
    b.power = 99999;
    b.cadence = -5;
    b.mode = 7;
    b.tune_road = 200;
    b.weight = 10;
    b.ftp = 9999;
    b.thr_hr = 0;
    b.ride_type = 99;
    CHECK(config_store_validate(&b), "越界数据被修正");
    CHECK(b.power == POWER_MAX, "功率夹到上限");
    CHECK(b.cadence == CADENCE_MIN, "踏频夹到下限");
    CHECK(b.mode == DEFAULT_RIDE_MODE, "非法模式回默认");
    CHECK(b.tune_road == MODE_TUNE_MAX, "road调节夹上限");
    CHECK(b.weight == PROFILE_WEIGHT_MIN, "体重夹下限");
    CHECK(b.ftp == PROFILE_FTP_MAX, "FTP夹上限");
    CHECK(b.thr_hr == PROFILE_THR_HR_MIN, "阈值心率夹下限");
    CHECK(b.ride_type == PROFILE_RIDE_TYPE_DEFAULT, "非法配速机器人回默认");

    // 版本不匹配触发迁移重置
    config_data_t m;
    config_store_defaults(&m);
    m.schema_ver = 1;
    m.power = 500;
    CHECK(config_store_migrate(&m), "旧版本触发迁移");
    CHECK(m.schema_ver == CONFIG_VERSION, "迁移后版本=当前");
    CHECK(m.power == DEFAULT_POWER, "迁移后重置默认");
    // 当前版本不迁移
    CHECK(!config_store_migrate(&m), "当前版本不迁移");
}

int main(void)
{
    test_profile();
    test_ftp_zone();
    test_hr_smooth();
    test_emit_deadband();
    test_adjust_power();
    test_erg();
    test_road_pacer();
    test_steady_snapshot();
    test_config_store();
    printf("\n结果: %d 通过, %d 失败\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
