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
#include "json_miniparse.h"
#include "button_logic.h"
#include "led_pattern.h"
#include "button_debounce.h"

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
    CHECK(p.ride_type == PACER_INDEX_DEFAULT, "默认配速机器人 coco");

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
    bool ok = power_engine_set_erg_target(&e, 250);
    CHECK(ok == true, "ERG合法目标返回true");
    CHECK(e.mode == RIDE_MODE_ERG, "ERG下发自动切ERG");
    CHECK(e.has_erg_target && e.erg_active_target == 250, "ERG目标=250");
    CHECK(e.base_power == 250, "base同步=250");
    // 超范围拒绝（高于上限）
    ok = power_engine_set_erg_target(&e, 9999);
    CHECK(ok == false, "超范围ERG返回false");
    CHECK(e.erg_active_target == 250, "超范围ERG拒绝（值不变）");
    // 超范围拒绝（低于下限，负值）
    ok = power_engine_set_erg_target(&e, -1);
    CHECK(ok == false, "负值ERG返回false");
    CHECK(e.erg_active_target == 250, "负值ERG拒绝（值不变）");
    // ERG 快照功率围绕目标小幅波动
    domain_rng_seed(42);
    for (int i = 0; i < 50; i++) {
        snapshot_t s = power_engine_snapshot(&e);
        CHECK(s.power >= 250 + STEADY_POWER_FLUCT_LO && s.power <= 250 + STEADY_POWER_FLUCT_HI,
              "ERG功率在目标±5内");
        CHECK(s.mode == RIDE_MODE_ERG, "快照模式=ERG");
    }
    // 同值幂等：已在 ERG 且目标未变时不置 dirty，避免高频同值反复落盘
    power_engine_clear_dirty(&e);
    ok = power_engine_set_erg_target(&e, 250);
    CHECK(ok == true, "同值ERG返回true");
    CHECK(!power_engine_is_dirty(&e), "同值ERG不置dirty");
    ok = power_engine_set_erg_target(&e, 260);
    CHECK(ok == true, "新值ERG返回true");
    CHECK(power_engine_is_dirty(&e), "新值ERG置dirty");
    CHECK(e.base_power == 260, "新值base同步=260");
}

static void test_setters_edge(void)
{
    printf("[test] setter 边界与拒绝路径\n");
    power_engine_t e;
    power_engine_init(&e);

    // set_mode 非法值拒绝
    CHECK(power_engine_set_mode(&e, (ride_mode_t)(-1)) == false, "set_mode 负值拒绝");
    CHECK(power_engine_set_mode(&e, (ride_mode_t)RIDE_MODE_COUNT) == false, "set_mode 越界拒绝");
    CHECK(power_engine_set_mode(&e, RIDE_MODE_STEADY) == false, "set_mode 同值拒绝");

    // set_device_name 空指针/空串拒绝、同名拒绝、超长截断
    CHECK(power_engine_set_device_name(&e, NULL) == false, "设备名NULL拒绝");
    CHECK(power_engine_set_device_name(&e, "") == false, "设备名空串拒绝");
    CHECK(power_engine_set_device_name(&e, "MyBike") == true, "设备名设置成功");
    CHECK(power_engine_set_device_name(&e, "MyBike") == false, "设备名同名拒绝");
    char longname[64];
    memset(longname, 'A', sizeof(longname));
    longname[63] = '\0';
    CHECK(power_engine_set_device_name(&e, longname) == true, "超长设备名接受");
    CHECK((int)strlen(e.device_name) == DEVICE_NAME_MAX - 1, "超长设备名截断到MAX-1");

    // set_cadence / adjust_cadence 仅 steady + 范围 + 同值
    power_engine_init(&e);
    CHECK(power_engine_set_cadence(&e, CADENCE_MAX + 50) == false, "踏频超上限拒绝");
    CHECK(power_engine_set_cadence(&e, CADENCE_MIN - 5) == false, "踏频低于下限拒绝");
    CHECK(power_engine_set_cadence(&e, e.cadence_base) == false, "踏频同值拒绝");
    CHECK(power_engine_set_cadence(&e, 100) == true, "踏频合法设置成功");
    power_engine_set_mode(&e, RIDE_MODE_ROAD);
    int cad_before = e.cadence_base;
    CHECK(power_engine_adjust_cadence(&e, 10) == cad_before, "非steady调踏频不变");

    // set_heartrate 仅 steady + 范围
    power_engine_init(&e);
    CHECK(power_engine_set_heartrate(&e, HR_MAX + 10) == false, "心率超上限拒绝");
    CHECK(power_engine_set_heartrate(&e, HR_MIN - 10) == false, "心率低于下限拒绝");
    CHECK(power_engine_set_heartrate(&e, 160) == true, "心率合法设置成功");
    CHECK(e.has_user_hr && e.user_heartrate == 160, "has_user_hr置位且值=160");
    power_engine_set_mode(&e, RIDE_MODE_ROAD);
    CHECK(power_engine_set_heartrate(&e, 150) == false, "非steady心率拒绝");
    // 持久化恢复场景：先设置心率再切非STEADY模式，字段保留不丢失，切回STEADY可直接使用
    CHECK(e.has_user_hr && e.user_heartrate == 160, "切到ROAD后固定心率字段保留");
    power_engine_set_mode(&e, RIDE_MODE_ERG);
    CHECK(e.has_user_hr && e.user_heartrate == 160, "切到ERG后固定心率字段保留");
    power_engine_set_mode(&e, RIDE_MODE_STEADY);
    CHECK(e.has_user_hr && e.user_heartrate == 160, "切回STEADY后固定心率字段保留，可直接使用");

    // clear_user_hr：解锁后恢复按 FTP 分区映射心率
    power_engine_clear_user_hr(&e);
    CHECK(!e.has_user_hr && e.user_heartrate == 0, "解锁后 has_user_hr 清零且 user_heartrate 归零");
    // 重复解锁幂等
    power_engine_clear_user_hr(&e);
    CHECK(!e.has_user_hr, "重复解锁幂等无副作用");
    // 重新锁定后再解锁，快照心率应恢复按 FTP 分区推导
    CHECK(power_engine_set_heartrate(&e, 170) == true, "重新锁定心率成功");
    CHECK(e.has_user_hr, "重新锁定后 has_user_hr 置位");
    power_engine_clear_user_hr(&e);
    power_engine_snapshot(&e);
    CHECK(!e.has_user_hr, "解锁后快照不触发重新锁定");
}

static void test_hr_variation_steady(void)
{
    printf("[test] HRV 漂移上限 + steady_user 区间钳制\n");
    hr_state_t st;
    hr_model_init(&st, 120);
    domain_rng_seed(7);
    // HRV 长时间累加后 drift 被 cap，整体波动幅度有界
    float last = 0.0f;
    for (int i = 0; i < 5000; i++) last = hr_model_variation(&st);
    CHECK(last <= HRV_RSA_AMPLITUDE + HRV_MAYER_AMPLITUDE + HRV_DRIFT_CAP_BPM + 0.01f,
          "HRV 总波动受 drift cap 限制");

    // steady_user：输出始终在 base ± 浮动幅度区间内
    hr_model_init(&st, 150);
    for (int i = 0; i < 300; i++) {
        int hr = hr_model_steady_user(&st, 150, HR_FLOAT_DEFAULT);
        CHECK(hr >= 150 - HR_FLOAT_DEFAULT && hr <= 150 + HR_FLOAT_DEFAULT,
              "steady_user 在区间内");
    }
    // amp=0 恒定输出
    hr_model_init(&st, 150);
    for (int i = 0; i < 20; i++) {
        int hr = hr_model_steady_user(&st, 155, 0);
        CHECK(hr == 155, "steady_user amp=0 恒定");
    }
}

static void test_road_pacer(void)
{
    printf("[test] road 配速功率范围\n");
    power_engine_t e;
    power_engine_init(&e);
    CHECK(power_engine_get_pacer_index(&e) == PACER_INDEX_DEFAULT, "默认配速机器人索引");
    CHECK(strcmp(power_engine_get_current_pacer(&e), "Coco") == 0, "默认配速机器人名称");
    CHECK(power_engine_set_pacer_index(&e, 2), "可切换到 Jacques");
    CHECK(power_engine_get_pacer_index(&e) == 2, "Jacques 索引=2");
    CHECK(strcmp(power_engine_get_current_pacer(&e), "Jacques") == 0, "当前机器人名称已更新");
    CHECK(!power_engine_set_pacer_index(&e, 2), "重复设置返回false");
    CHECK(!power_engine_set_pacer_index(&e, RIDE_TYPE_COUNT), "非法机器人索引拒绝");
    profile_set_weight(&e.profile, 70);
    CHECK(power_engine_set_pacer_index(&e, PACER_INDEX_DEFAULT), "切回 Coco");
    power_engine_set_mode(&e, RIDE_MODE_ROAD);
    domain_rng_seed(7);
    int lo = 70 * 24 / 10; // 168
    int hi = 70 * 28 / 10; // 196
    for (int i = 0; i < 100; i++) {
        snapshot_t s = power_engine_snapshot(&e);
        CHECK(s.power >= lo && s.power <= hi, "coco 功率在功体比范围内");
    }

    // 自定义档：切到 custom，设范围 3.0~3.5 W/kg，功率应落在 weight*wkg 区间
    CHECK(power_engine_set_pacer_index(&e, PACER_INDEX_CUSTOM), "可切换到自定义档");
    CHECK(power_engine_get_pacer_index(&e) == PACER_INDEX_CUSTOM, "自定义档索引=5");
    CHECK(strcmp(power_engine_get_current_pacer(&e), "Custom") == 0, "自定义档名称=Custom");
    CHECK(profile_set_custom_wkg(&e.profile, 30, 35), "设自定义 W/kg 3.0~3.5");
    power_engine_set_mode(&e, RIDE_MODE_STEADY);
    power_engine_set_mode(&e, RIDE_MODE_ROAD);  // 重置模式状态按新范围
    domain_rng_seed(11);
    int clo = 70 * 30 / 10; // 210
    int chi = 70 * 35 / 10; // 245
    for (int i = 0; i < 100; i++) {
        snapshot_t s = power_engine_snapshot(&e);
        CHECK(s.power >= clo && s.power <= chi, "自定义档功率在 3.0~3.5 W/kg 范围内");
    }
    // 自定义档范围越界夹紧 + low<=high
    CHECK(profile_set_custom_wkg(&e.profile, 999, 5), "超界范围可设置（会夹紧）");
    CHECK(e.profile.custom_wkg_low == CUSTOM_WKG_MAX, "下限夹到上限值 CUSTOM_WKG_MAX");
    CHECK(e.profile.custom_wkg_high == CUSTOM_WKG_MAX, "上限被抬到不低于下限");
}

static void test_steady_snapshot(void)
{
    printf("[test] steady 快照功率浮动范围\n");
    power_engine_t e;
    power_engine_init(&e);
    e.base_power = 200;
    domain_rng_seed(99);
    for (int i = 0; i < 100; i++) {
        snapshot_t s = power_engine_snapshot(&e);
        CHECK(s.power >= 200 - e.power_float && s.power <= 200 + e.power_float,
              "steady 功率在 base±浮动内");
        CHECK(s.heartrate >= HR_MIN && s.heartrate <= HR_MAX, "心率合法范围");
        CHECK(s.cadence >= CADENCE_MIN && s.cadence <= CADENCE_MAX, "踏频合法范围");
    }
    // 浮动幅度=0 时功率恒定为基准值
    power_engine_init(&e);
    e.base_power = 180;
    power_engine_set_power_float(&e, 0);
    power_engine_set_cadence_float(&e, 0);
    for (int i = 0; i < 50; i++) {
        snapshot_t s = power_engine_snapshot(&e);
        CHECK(s.power == 180, "浮动=0 功率恒定");
        CHECK(s.cadence == e.cadence_base, "浮动=0 踏频恒定");
    }
}

static void test_float_setters(void)
{
    printf("[test] 浮动幅度 setter 边界\n");
    power_engine_t e;
    power_engine_init(&e);
    CHECK(e.power_float == POWER_FLOAT_DEFAULT, "默认功率浮动");
    CHECK(e.cadence_float == CADENCE_FLOAT_DEFAULT, "默认踏频浮动");
    CHECK(e.hr_float == HR_FLOAT_DEFAULT, "默认心率浮动");
    // 越界夹紧
    power_engine_set_power_float(&e, 99999);
    CHECK(e.power_float == POWER_FLOAT_MAX, "功率浮动夹上限");
    power_engine_set_power_float(&e, -5);
    CHECK(e.power_float == POWER_FLOAT_MIN, "功率浮动夹下限");
    // 同值不置 dirty
    power_engine_set_power_float(&e, 0);
    power_engine_clear_dirty(&e);
    CHECK(power_engine_set_power_float(&e, 0) == false, "功率浮动同值拒绝");
    CHECK(!power_engine_is_dirty(&e), "功率浮动同值不置 dirty");
    // 合法设置置 dirty
    CHECK(power_engine_set_cadence_float(&e, 10) == true, "踏频浮动合法设置");
    CHECK(e.cadence_float == 10 && power_engine_is_dirty(&e), "踏频浮动生效并置 dirty");
    CHECK(power_engine_set_hr_float(&e, 15) == true, "心率浮动合法设置");
    CHECK(e.hr_float == 15, "心率浮动生效");
    // 调功步进：默认/夹紧/同值/合法
    CHECK(e.power_step == POWER_STEP_DEFAULT, "默认调功步进");
    power_engine_set_power_step(&e, 99999);
    CHECK(e.power_step == POWER_STEP_MAX, "调功步进夹上限");
    power_engine_set_power_step(&e, 0);
    CHECK(e.power_step == POWER_STEP_MIN, "调功步进夹下限");
    power_engine_clear_dirty(&e);
    CHECK(power_engine_set_power_step(&e, POWER_STEP_MIN) == false, "调功步进同值拒绝");
    CHECK(!power_engine_is_dirty(&e), "调功步进同值不置 dirty");
    CHECK(power_engine_set_power_step(&e, 30) == true, "调功步进合法设置");
    CHECK(e.power_step == 30 && power_engine_is_dirty(&e), "调功步进生效并置 dirty");
}

static void test_engine_blindspots(void)
{
    printf("[test] 引擎/档案 盲区补强\n");
    power_engine_t e;

    // power_engine_set_power：仅 steady、范围、同值拒绝、成功置 dirty
    power_engine_init(&e);
    power_engine_clear_dirty(&e);
    CHECK(power_engine_set_power(&e, POWER_MAX + 100) == false, "set_power 超上限拒绝");
    CHECK(power_engine_set_power(&e, POWER_MIN - 1) == false, "set_power 低于下限拒绝");
    CHECK(power_engine_set_power(&e, e.base_power) == false, "set_power 同值拒绝");
    CHECK(!power_engine_is_dirty(&e), "set_power 拒绝不置 dirty");
    CHECK(power_engine_set_power(&e, 222) == true, "set_power 合法成功");
    CHECK(e.base_power == 222 && e.last_power == 222, "set_power 同步 base/last");
    CHECK(power_engine_is_dirty(&e), "set_power 成功置 dirty");
    power_engine_set_mode(&e, RIDE_MODE_ROAD);
    CHECK(power_engine_set_power(&e, 150) == false, "非 steady set_power 拒绝");

    // set_erg_target 同值幂等：已在 ERG 且目标未变，返回 true 但不置 dirty（防 flash 损耗）
    power_engine_init(&e);
    CHECK(power_engine_set_erg_target(&e, 200) == true, "首次 ERG 目标成功");
    CHECK(e.mode == RIDE_MODE_ERG && e.dirty, "ERG 切模式并置 dirty");
    power_engine_clear_dirty(&e);
    CHECK(power_engine_set_erg_target(&e, 200) == true, "同值 ERG 幂等返回 true");
    CHECK(!power_engine_is_dirty(&e), "同值 ERG 不置 dirty");
    CHECK(power_engine_set_erg_target(&e, 210) == true, "变更 ERG 目标成功");
    CHECK(power_engine_is_dirty(&e), "变更 ERG 置 dirty");

    // reset_mode_state：ERG 有目标时 last_power=目标，无目标时回退 base_power
    power_engine_init(&e);
    e.mode = RIDE_MODE_ERG;
    e.has_erg_target = true;
    e.erg_active_target = 333;
    power_engine_reset_mode_state(&e);
    CHECK(e.last_power == 333, "ERG reset 有目标 last_power=目标");
    e.has_erg_target = false;
    e.base_power = 180;
    power_engine_reset_mode_state(&e);
    CHECK(e.last_power == 180, "ERG reset 无目标回退 base_power");

    // set_pacer_index 在 ROAD 模式触发 reset（last_power 按新机器人功体比变化）
    power_engine_init(&e);
    profile_set_weight(&e.profile, 70);
    power_engine_set_mode(&e, RIDE_MODE_ROAD);
    power_engine_set_pacer_index(&e, 0);  // Coco wkg_low=24
    int coco_lp = e.last_power;
    CHECK(power_engine_set_pacer_index(&e, 3) == true, "ROAD 切 Genie 成功");
    CHECK(e.last_power != coco_lp, "ROAD 切机器人触发 reset 改变 last_power");

    // profile_set_thr_hr 边界（此前未独立覆盖）
    profile_t p;
    profile_init(&p);
    CHECK(!profile_set_thr_hr(&p, PROFILE_THR_HR_MIN - 1), "thr_hr 下限拒绝");
    CHECK(!profile_set_thr_hr(&p, PROFILE_THR_HR_MAX + 1), "thr_hr 上限拒绝");
    CHECK(profile_set_thr_hr(&p, PROFILE_THR_HR_MIN), "thr_hr 边界合法");
    CHECK(!profile_set_thr_hr(&p, PROFILE_THR_HR_MIN), "thr_hr 同值拒绝");

    // profile_ride_type_index NULL/未知回退默认
    CHECK(profile_ride_type_index(NULL) == PACER_INDEX_DEFAULT, "ride_type_index NULL 回退默认");
    CHECK(profile_ride_type_index("nope") == PACER_INDEX_DEFAULT, "ride_type_index 未知回退默认");
    CHECK(profile_ride_type_index("coco") == 0, "ride_type_index coco=0");

    // profile_set_ride_type_str 空串拒绝
    CHECK(!profile_set_ride_type_str(&p, ""), "ride_type_str 空串拒绝");
    CHECK(!profile_set_ride_type_str(&p, NULL), "ride_type_str NULL 拒绝");
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
    CHECK(c.heartrate == DEFAULT_HEARTRATE, "默认心率");
    CHECK(c.has_user_hr == 0, "默认不启用固定心率");
    CHECK(c.weight == PROFILE_WEIGHT_DEFAULT, "默认体重");
    CHECK(c.ftp == PROFILE_FTP_DEFAULT, "默认FTP");
    CHECK(c.thr_hr == PROFILE_THR_HR_DEFAULT, "默认阈值心率");
    CHECK(c.ride_type == PACER_INDEX_DEFAULT, "默认配速机器人");
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
    b.heartrate = 9999;
    b.weight = 10;
    b.ftp = 9999;
    b.thr_hr = 0;
    b.ride_type = 99;
    CHECK(config_store_validate(&b), "越界数据被修正");
    CHECK(b.power == POWER_MAX, "功率夹到上限");
    CHECK(b.cadence == CADENCE_MIN, "踏频夹到下限");
    CHECK(b.mode == DEFAULT_RIDE_MODE, "非法模式回默认");
    CHECK(b.heartrate == HR_MAX, "心率夹到上限");
    CHECK(b.weight == PROFILE_WEIGHT_MIN, "体重夹下限");
    CHECK(b.ftp == PROFILE_FTP_MAX, "FTP夹上限");
    CHECK(b.thr_hr == PROFILE_THR_HR_MIN, "阈值心率夹下限");
    CHECK(b.ride_type == PACER_INDEX_DEFAULT, "非法配速机器人回默认");

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

    // 固定心率：启用并设合法值，validate 不修正、值保留（持久化往返关键）
    config_data_t hr;
    config_store_defaults(&hr);
    hr.has_user_hr = 1;
    hr.heartrate = 165;
    CHECK(!config_store_validate(&hr), "启用固定心率合法不修正");
    CHECK(hr.has_user_hr == 1 && hr.heartrate == 165, "固定心率字段保留");
    // has_user_hr 非 0/1 被规范化为 1
    config_data_t hr2;
    config_store_defaults(&hr2);
    hr2.has_user_hr = 5;
    CHECK(config_store_validate(&hr2), "has_user_hr 非规范值被修正");
    CHECK(hr2.has_user_hr == 1, "has_user_hr 规范化为1");

    // ===== 序列化 / 反序列化 往返 =====
    char js[352];
    config_data_t src;
    config_store_defaults(&src);
    src.power = 222;
    src.cadence = 88;
    src.ftp = 250;
    src.power_float = 12;
    src.cadence_float = 7;
    src.hr_float = 9;
    src.power_step = 25;
    strcpy(src.device_name, "MyBike");
    size_t wlen = config_store_serialize(&src, js, sizeof(js));
    CHECK(wlen > 0 && wlen < sizeof(js), "serialize 返回合法长度");
    CHECK(js[wlen] == '\0' && strlen(js) == wlen, "serialize 以 '\\0' 结尾且长度一致");
    CHECK(strstr(js, "\"power\":222") != NULL, "serialize 含 power=222");
    CHECK(strstr(js, "\"power_float\":12") != NULL, "serialize 含 power_float=12");
    CHECK(strstr(js, "\"power_step\":25") != NULL, "serialize 含 power_step=25");
    CHECK(strstr(js, "\"device_name\":\"MyBike\"") != NULL, "serialize 含设备名");

    config_data_t dst;
    CHECK(config_store_deserialize(js, &dst), "deserialize 成功");
    CHECK(dst.power == 222 && dst.cadence == 88 && dst.ftp == 250, "往返整数字段一致");
    CHECK(dst.power_float == 12 && dst.cadence_float == 7 && dst.hr_float == 9, "往返浮动字段一致");
    CHECK(dst.power_step == 25, "往返调功步进一致");
    CHECK(strcmp(dst.device_name, "MyBike") == 0, "往返设备名一致");
    CHECK(dst.schema_ver == src.schema_ver && dst.mode == src.mode, "往返其余字段一致");

    // 缺键：仅覆盖出现的字段，其余保持默认
    config_data_t partial;
    CHECK(config_store_deserialize("{\"schema_ver\":5,\"power\":300}", &partial),
          "部分键 JSON deserialize 成功");
    CHECK(partial.power == 300, "出现的 power 被覆盖");
    CHECK(partial.cadence == DEFAULT_CADENCE, "缺失的 cadence 保持默认");
    CHECK(strcmp(partial.device_name, DEVICE_NAME_DEFAULT) == 0, "缺失设备名保持默认");

    // 非法 JSON（无 schema_ver）失败且不污染原对象
    config_data_t guard;
    config_store_defaults(&guard);
    guard.power = 123;
    CHECK(!config_store_deserialize("{\"power\":999}", &guard), "缺 schema_ver -> 失败");
    CHECK(guard.power == 123, "失败时原对象不被修改");
    CHECK(!config_store_deserialize(NULL, &guard), "NULL json -> 失败");
    CHECK(!config_store_deserialize("{}", &guard), "空对象 -> 失败");

    // 越界值经反序列化后被 validate 夹紧
    config_data_t clamp;
    CHECK(config_store_deserialize("{\"schema_ver\":5,\"power\":99999}", &clamp),
          "越界 power JSON deserialize 成功");
    CHECK(clamp.power == POWER_MAX, "反序列化后 power 被夹到上限");

    // 特殊字符转义往返：含双引号、反斜杠
    config_data_t spec;
    config_store_defaults(&spec);
    strcpy(spec.device_name, "a\"b\\c");
    CHECK(config_store_serialize(&spec, js, sizeof(js)) > 0, "含特殊字符 serialize 成功");
    CHECK(strstr(js, "a\\\"b\\\\c") != NULL, "特殊字符被正确转义");
    config_data_t spec2;
    CHECK(config_store_deserialize(js, &spec2), "含转义 JSON deserialize 成功");
    CHECK(strcmp(spec2.device_name, "a\"b\\c") == 0, "转义字符往返还原");

    // 缓冲区不足返回 0，不溢出
    char tiny[8];
    CHECK(config_store_serialize(&src, tiny, sizeof(tiny)) == 0, "缓冲不足 serialize 返回 0");

    // NULL 防御
    CHECK(config_store_serialize(NULL, js, sizeof(js)) == 0, "serialize NULL config -> 0");
    CHECK(config_store_serialize(&src, NULL, sizeof(js)) == 0, "serialize NULL out -> 0");
    CHECK(config_store_serialize(&src, js, 0) == 0, "serialize out_sz=0 -> 0");
}

static void test_json_miniparse(void)
{
    printf("[test] JSON 最小解析\n");
    const char *json = "{\"version\":\"3.1.8\",\"url\":\"https://gitee.com/x/f.bin\",\"size\":102400,\"changelog\":\"修复 OTA\"}";
    char out[128];
    int n;

    CHECK(json_get_str(json, "version", out, sizeof(out)), "提取 version");
    CHECK(strcmp(out, "3.1.8") == 0, "version 值正确");

    CHECK(json_get_str(json, "url", out, sizeof(out)), "提取 url");
    CHECK(strcmp(out, "https://gitee.com/x/f.bin") == 0, "url 值正确");

    CHECK(json_get_str(json, "changelog", out, sizeof(out)), "提取 changelog");
    CHECK(strcmp(out, "修复 OTA") == 0, "changelog 值正确（中文）");

    n = json_get_int(json, "size");
    CHECK(n == 102400, "size 整数值正确");

    n = json_get_int(json, "nonexistent");
    CHECK(n == 0, "不存在键返回 0");

    CHECK(!json_get_str(json, "nonexistent", out, sizeof(out)), "不存在键返回 false");

    // 缓冲区不足截断
    char small[5];
    CHECK(json_get_str(json, "version", small, sizeof(small)), "小缓冲提取");
    CHECK(strcmp(small, "3.1.") == 0, "小缓冲截断为 3.1.");

    // 嵌套 JSON（key 出现在 value 中）
    const char *nested = "{\"a\":\"1\",\"key\":\"val\"}";
    CHECK(json_get_str(nested, "key", out, sizeof(out)), "嵌套场景提取");
    CHECK(strcmp(out, "val") == 0, "嵌套值正确");

    // 空字符串值
    const char *empty = "{\"v\":\"\"}";
    CHECK(json_get_str(empty, "v", out, sizeof(out)), "空字符串值提取");
    CHECK(out[0] == '\0', "空字符串值正确");

    // 负数
    const char *neg = "{\"x\":-42}";
    CHECK(json_get_int(neg, "x") == -42, "负数解析");
}

static void test_button_logic(void)
{
    printf("[test] 按钮按压时长分类\n");
    const uint32_t SHORT = 500;
    const uint32_t HOLD = 2000;
    const uint32_t WINDOW = 3000;

    // button_classify_release 边界
    CHECK(button_classify_release(0, SHORT, HOLD) == BTN_RELEASE_SHORT, "0ms 短按");
    CHECK(button_classify_release(499, SHORT, HOLD) == BTN_RELEASE_SHORT, "499ms 短按");
    CHECK(button_classify_release(500, SHORT, HOLD) == BTN_RELEASE_MID, "500ms 边界归 MID（不减功）");
    CHECK(button_classify_release(1999, SHORT, HOLD) == BTN_RELEASE_MID, "1999ms 中间区间");
    CHECK(button_classify_release(2000, SHORT, HOLD) == BTN_RELEASE_LONG, "2000ms 边界归 LONG（触发确认）");
    CHECK(button_classify_release(5000, SHORT, HOLD) == BTN_RELEASE_LONG, "5000ms 长按");

    // button_repeat_should_adjust 边界
    CHECK(button_repeat_should_adjust(0, HOLD) == true, "0ms REPEAT 调功");
    CHECK(button_repeat_should_adjust(1999, HOLD) == true, "1999ms REPEAT 调功");
    CHECK(button_repeat_should_adjust(2000, HOLD) == false, "2000ms REPEAT 停止调功（进入确认区间）");
    CHECK(button_repeat_should_adjust(3000, HOLD) == false, "3000ms REPEAT 停止调功");

    // button_confirm_window_expired 边界
    CHECK(button_confirm_window_expired(0, WINDOW) == false, "0ms 未超时");
    CHECK(button_confirm_window_expired(2999, WINDOW) == false, "2999ms 未超时");
    CHECK(button_confirm_window_expired(3000, WINDOW) == true, "3000ms 边界超时");
    CHECK(button_confirm_window_expired(5000, WINDOW) == true, "5000ms 已超时");

    // 自定义阈值（验证不硬编码）
    CHECK(button_classify_release(100, 100, 300) == BTN_RELEASE_MID, "自定义阈值 100ms 边界归 MID");
    CHECK(button_classify_release(300, 100, 300) == BTN_RELEASE_LONG, "自定义阈值 300ms 边界归 LONG");
    CHECK(button_repeat_should_adjust(300, 300) == false, "自定义阈值 300ms REPEAT 停止");
    CHECK(button_confirm_window_expired(100, 100) == true, "自定义阈值 100ms 超时");
}

static void test_led_pattern(void)
{
    printf("[test] LED 模式映射\n");
    const uint8_t B = 0x30;
    uint8_t rgb[3];

    // led_pattern_to_rgb 颜色映射
    led_pattern_to_rgb(LED_PATTERN_OFF, B, rgb);
    CHECK(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0, "OFF 全灭");

    led_pattern_to_rgb(LED_PATTERN_SLOW, B, rgb);
    CHECK(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == B, "SLOW 蓝色 [0,0,B]");

    led_pattern_to_rgb(LED_PATTERN_SOLID, B, rgb);
    CHECK(rgb[0] == B && rgb[1] == 0 && rgb[2] == 0, "SOLID 绿色 [B,0,0]");

    led_pattern_to_rgb(LED_PATTERN_FAST, B, rgb);
    CHECK(rgb[0] == B && rgb[1] == B && rgb[2] == 0, "FAST 黄色 [B,B,0]");

    // 亮度参数生效
    led_pattern_to_rgb(LED_PATTERN_SLOW, 0x10, rgb);
    CHECK(rgb[2] == 0x10, "SLOW 自定义亮度");

    // led_blink_should_toggle 节拍
    CHECK(led_blink_should_toggle(LED_PATTERN_OFF, 5000, 1000, 200) == false, "OFF 不闪烁");
    CHECK(led_blink_should_toggle(LED_PATTERN_SOLID, 5000, 1000, 200) == false, "SOLID 不闪烁");
    CHECK(led_blink_should_toggle(LED_PATTERN_SLOW, 999, 1000, 200) == false, "SLOW 999ms 未到周期");
    CHECK(led_blink_should_toggle(LED_PATTERN_SLOW, 1000, 1000, 200) == true, "SLOW 1000ms 边界翻转");
    CHECK(led_blink_should_toggle(LED_PATTERN_FAST, 199, 1000, 200) == false, "FAST 199ms 未到周期");
    CHECK(led_blink_should_toggle(LED_PATTERN_FAST, 200, 1000, 200) == true, "FAST 200ms 边界翻转");

    // led_pattern_initial_on 初始状态
    CHECK(led_pattern_initial_on(LED_PATTERN_OFF) == 0, "OFF 初始灭");
    CHECK(led_pattern_initial_on(LED_PATTERN_SLOW) == 0, "SLOW 初始灭");
    CHECK(led_pattern_initial_on(LED_PATTERN_SOLID) == 1, "SOLID 初始亮");
    CHECK(led_pattern_initial_on(LED_PATTERN_FAST) == 0, "FAST 初始灭");
}

static void test_button_debounce(void)
{
    printf("[test] 按钮去抖逻辑\n");
    const uint32_t SHORT = 500;
    const uint32_t REPEAT = 200;

    // button_debounce_is_valid_press
    CHECK(button_debounce_is_valid_press(true) == true, "去抖后仍按下 -> 有效");
    CHECK(button_debounce_is_valid_press(false) == false, "去抖后已松开 -> 误触");

    // button_repeat_should_fire 边界
    CHECK(button_repeat_should_fire(0, 0, SHORT, REPEAT) == false, "0ms < SHORT 不触发");
    CHECK(button_repeat_should_fire(499, 0, SHORT, REPEAT) == false, "499ms < SHORT 不触发");
    CHECK(button_repeat_should_fire(500, 0, SHORT, REPEAT) == true, "500ms 边界首次触发");
    CHECK(button_repeat_should_fire(600, 500, SHORT, REPEAT) == false, "距上次 100ms < REPEAT 不触发");
    CHECK(button_repeat_should_fire(700, 500, SHORT, REPEAT) == true, "距上次 200ms 边界触发");
    CHECK(button_repeat_should_fire(1000, 800, SHORT, REPEAT) == true, "距上次 200ms 触发");

    // button_release_is_real
    CHECK(button_release_is_real(false) == true, "去抖后已松开 -> 真释放");
    CHECK(button_release_is_real(true) == false, "去抖后仍按下 -> 抖动");
}

int main(void)
{
    test_profile();
    test_ftp_zone();
    test_hr_smooth();
    test_emit_deadband();
    test_adjust_power();
    test_erg();
    test_setters_edge();
    test_hr_variation_steady();
    test_road_pacer();
    test_steady_snapshot();
    test_float_setters();
    test_engine_blindspots();
    test_config_store();
    test_json_miniparse();
    test_button_logic();
    test_led_pattern();
    test_button_debounce();
    printf("\n结果: %d 通过, %d 失败\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
