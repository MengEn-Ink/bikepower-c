/**
 * 领域层 host 单元测试（纯 C，clang/gcc 直接编译运行）
 * 验证 power_engine / ride_modes / hr_model / profile 的数学行为。
 * 编译: make -C test  或  clang -I../components/.../include ...
 */
#include <stdio.h>
#include <stdlib.h>
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
#include "device_template.h"
#include "device_analysis.h"
#include "running_engine.h"
#include "ble_config_codec.h"
#include "device_identity.h"

static int g_pass = 0;
static int g_fail = 0;

#define RIDE_SIM_SECONDS 5400
#define RIDE_SIM_TICKS_PER_SECOND (1000 / HR_LOOP_INTERVAL_MS)
#define LONG_RIDE_SIM_HOURS 24
#define LONG_RIDE_SIM_TICKS ((uint64_t)LONG_RIDE_SIM_HOURS * 60u * 60u * RIDE_SIM_TICKS_PER_SECOND)

static int s_sim_power[RIDE_SIM_SECONDS];
static int s_sim_cadence[RIDE_SIM_SECONDS];
static int s_sim_hr[RIDE_SIM_SECONDS];
static int s_sim_delta[RIDE_SIM_SECONDS - 1];

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

static int compare_int(const void *a, const void *b)
{
    int lhs = *(const int *)a;
    int rhs = *(const int *)b;
    return (lhs > rhs) - (lhs < rhs);
}

static double series_mean(const int *values, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; i++) sum += values[i];
    return sum / count;
}

static double series_stddev(const int *values, int count, double mean)
{
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        double delta = values[i] - mean;
        sum += delta * delta;
    }
    return sqrt(sum / count);
}

static double series_correlation(const int *lhs, const int *rhs, int count, int lag)
{
    int pairs = count - lag;
    double lhs_mean = series_mean(lhs, pairs);
    double rhs_mean = series_mean(rhs + lag, pairs);
    double covariance = 0.0;
    double lhs_variance = 0.0;
    double rhs_variance = 0.0;
    for (int i = 0; i < pairs; i++) {
        double lhs_delta = lhs[i] - lhs_mean;
        double rhs_delta = rhs[i + lag] - rhs_mean;
        covariance += lhs_delta * rhs_delta;
        lhs_variance += lhs_delta * lhs_delta;
        rhs_variance += rhs_delta * rhs_delta;
    }
    if (lhs_variance <= 0.0 || rhs_variance <= 0.0) return 0.0;
    return covariance / sqrt(lhs_variance * rhs_variance);
}

static int absolute_delta_percentile(const int *values, int count, int percentile)
{
    for (int i = 1; i < count; i++) {
        int delta = values[i] - values[i - 1];
        s_sim_delta[i - 1] = delta < 0 ? -delta : delta;
    }
    qsort(s_sim_delta, (size_t)(count - 1), sizeof(s_sim_delta[0]), compare_int);
    int index = (count - 2) * percentile / 100;
    return s_sim_delta[index];
}

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

    hr_state_t load_up, load_down;
    hr_model_init(&load_up, 100);
    hr_model_init(&load_down, 100);
    hr_model_reset_load(&load_up, 100);
    hr_model_reset_load(&load_down, 200);
    for (int i = 0; i < RIDE_SIM_TICKS_PER_SECOND; i++) {
        hr_model_smooth_load(&load_up, 200);
        hr_model_smooth_load(&load_down, 100);
    }
    CHECK(load_up.load_power > 100.0f && load_up.load_power < 200.0f,
          "负荷功率上升具有惯性");
    CHECK(load_down.load_power > 100.0f && load_down.load_power < 200.0f,
          "负荷功率下降具有惯性");
    CHECK((200.0f - load_down.load_power) < (load_up.load_power - 100.0f),
          "负荷下降时间常数大于上升时间常数");
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
    printf("[test] ERG 会话目标与持久配置隔离\n");
    power_engine_t e;
    power_engine_init(&e);
    int base_before = e.base_power;
    bool ok = power_engine_set_erg_target(&e, 250);
    CHECK(ok == true, "ERG合法目标返回true");
    CHECK(e.mode == RIDE_MODE_ERG, "ERG下发自动切ERG");
    CHECK(e.has_erg_target && e.erg_active_target == 250, "ERG目标=250");
    CHECK(e.base_power == base_before, "ERG目标不覆盖持久base功率");
    CHECK(!power_engine_is_dirty(&e), "首次ERG目标不置dirty");
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
        CHECK(s.power >= 250 - ERG_POWER_FLOAT && s.power <= 250 + ERG_POWER_FLOAT,
              "ERG功率在目标±5内");
        CHECK(s.mode == RIDE_MODE_ERG, "快照模式=ERG");
    }
    // ERG 是运行时会话态：同值和变化目标都不触发 NVS dirty。
    ok = power_engine_set_erg_target(&e, 250);
    CHECK(ok == true, "同值ERG返回true");
    CHECK(!power_engine_is_dirty(&e), "同值ERG不置dirty");
    ok = power_engine_set_erg_target(&e, 260);
    CHECK(ok == true, "新值ERG返回true");
    CHECK(!power_engine_is_dirty(&e), "新值ERG仍不置dirty");
    CHECK(e.base_power == base_before, "新值ERG仍保留base功率");

    CHECK(power_engine_clear_erg_target(&e) == true, "清除ERG目标返回true");
    CHECK(e.mode == RIDE_MODE_STEADY, "清除ERG后回到固定功率模式");
    CHECK(!e.has_erg_target && e.erg_active_target == 0, "清除ERG目标状态");
    CHECK(e.base_power == base_before && e.last_power == base_before, "清除ERG恢复进入前base功率");
    CHECK(!power_engine_is_dirty(&e), "清除ERG不置dirty");
    CHECK(power_engine_set_power(&e, 180) == true, "清除ERG后手动设功率恢复生效");
    CHECK(e.base_power == 180 && e.last_power == 180, "清除ERG后功率可更新");
    power_engine_clear_dirty(&e);
    CHECK(power_engine_clear_erg_target(&e) == false, "重复清除ERG不置变更");
    CHECK(!power_engine_is_dirty(&e), "重复清除ERG保持clean");

    CHECK(power_engine_set_erg_target(&e, 240) == true, "再次进入ERG成功");
    CHECK(e.base_power == 180 && !power_engine_is_dirty(&e), "再次进入ERG仍保留手动功率且不置dirty");
    CHECK(power_engine_set_mode(&e, RIDE_MODE_STEADY) == true, "切回固定功率成功");
    CHECK(!e.has_erg_target && e.erg_active_target == 0, "切回非ERG清除旧目标");
}

static void test_setters_edge(void)
{
    printf("[test] setter 边界与拒绝路径\n");
    power_engine_t e;
    power_engine_init(&e);

    // set_mode 非法值拒绝
    CHECK(power_engine_set_mode(&e, (ride_mode_t)(-1)) == false, "set_mode 负值拒绝");
    CHECK(power_engine_set_mode(&e, (ride_mode_t)RIDE_MODE_COUNT) == false, "set_mode 越界拒绝");
    CHECK(power_engine_set_mode(&e, RIDE_MODE_ERG) == false, "set_mode 拒绝持久设置ERG会话态");
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
    CHECK(power_engine_set_erg_target(&e, 200), "ERG会话目标设置成功");
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

    power_engine_init(&e);
    power_engine_set_mode(&e, RIDE_MODE_ROAD);
    CHECK(power_engine_set_heartrate(&e, 150) == false,
          "骑行机器人多项广播时心率仍保持联动，不允许手动锁定");
    CHECK(power_engine_set_broadcast_mask(&e, BROADCAST_FIELD_HEARTRATE), "切到只广播心率");
    CHECK(power_engine_set_heartrate(&e, 150) == true, "骑行只剩心率时允许直接设置心率");

    power_engine_init(&e);
    CHECK(power_engine_set_sport_profile(&e, SPORT_PROFILE_RUNNING), "切到跑步模式");
    CHECK(power_engine_set_heartrate(&e, 145) == true, "跑步模式允许锁定目标心率");
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
    float variation_limit = HRV_RSA_AMPLITUDE + HRV_MAYER_AMPLITUDE
                          + HRV_DRIFT_CAP_BPM + HRV_WANDER_AMPLITUDE;
    CHECK(last >= -variation_limit - 0.01f && last <= variation_limit + 0.01f,
          "HRV 总波动受 drift cap 限制");
    hr_model_reset_variation(&st);
    CHECK(st.hrv_tick == 0 && st.hrv_sec_counter == 0 && st.hrv_cached == 0.0f,
          "HRV reset 清除周期缓存");
    CHECK(st.wander_value == 0.0f && st.wander_hold == 0
          && st.wander_duration == 0 && st.wander_elapsed == 0,
          "HRV reset 清除慢速游走状态");

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
    for (int second = 0; second < 120; second++) {
        snapshot_t s = { 0 };
        for (int tick = 0; tick < RIDE_SIM_TICKS_PER_SECOND; tick++) {
            s = power_engine_snapshot(&e);
        }
        s_sim_power[second] = s.power;
    }
    CHECK(series_correlation(s_sim_power, s_sim_power, 120, 1) > 0.75,
          "road功率具有秒级连续性而非独立随机跳变");

    // 自定义档：切到 custom，设范围 3.0~3.5 W/kg，功率应落在 weight*wkg 区间
    CHECK(power_engine_set_pacer_index(&e, PACER_INDEX_CUSTOM), "可切换到自定义档");
    CHECK(power_engine_get_pacer_index(&e) == PACER_INDEX_CUSTOM, "自定义档索引=5");
    CHECK(strcmp(power_engine_get_current_pacer(&e), CUSTOM_PACER_NAME_DEFAULT) == 0, "自定义档名称=默认昵称");
    CHECK(profile_set_custom_name(&e.profile, "兔子车手"), "可设置自定义机器人昵称");
    CHECK(strcmp(power_engine_get_current_pacer(&e), "兔子车手") == 0, "自定义档名称使用用户昵称");
    CHECK(!profile_set_custom_name(&e.profile, "兔子车手"), "重复设置自定义昵称返回false");
    CHECK(!profile_set_custom_name(&e.profile, ""), "空自定义昵称拒绝");
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

static void test_broadcast_fields_and_button_target(void)
{
    printf("[test] 广播数据项开关与按钮目标选择\n");
    power_engine_t e;
    power_engine_init(&e);
    CHECK(e.broadcast_mask == BROADCAST_FIELD_ALL, "默认广播功率/踏频/心率全开");
    CHECK(power_engine_broadcast_enabled(&e, BROADCAST_FIELD_POWER), "默认功率广播开启");
    CHECK(power_engine_broadcast_enabled(&e, BROADCAST_FIELD_CADENCE), "默认踏频广播开启");
    CHECK(power_engine_broadcast_enabled(&e, BROADCAST_FIELD_HEARTRATE), "默认心率广播开启");

    power_engine_clear_dirty(&e);
    CHECK(!power_engine_set_broadcast_mask(&e, 0), "拒绝关闭全部广播项");
    CHECK(e.broadcast_mask == BROADCAST_FIELD_ALL, "全关被拒绝后保留原 mask");
    CHECK(!power_engine_is_dirty(&e), "全关被拒绝不置 dirty");

    CHECK(power_engine_set_broadcast_mask(&e, BROADCAST_FIELD_HEARTRATE), "可只保留心率广播");
    CHECK(e.broadcast_mask == BROADCAST_FIELD_HEARTRATE, "只保留心率 mask 生效");
    CHECK(power_engine_button_target(&e) == CONTROL_TARGET_HEARTRATE, "只剩心率时按钮目标为心率");

    power_engine_set_sport_profile(&e, SPORT_PROFILE_RUNNING);
    CHECK(power_engine_set_broadcast_mask(&e, BROADCAST_FIELD_CADENCE), "跑步可只保留步频配置项");
    CHECK(power_engine_button_target(&e) == CONTROL_TARGET_RUN_CADENCE, "跑步只剩步频时按钮目标为跑步步频");
    CHECK(power_engine_set_broadcast_mask(&e, BROADCAST_FIELD_POWER | BROADCAST_FIELD_HEARTRATE), "跑步配速+心率 mask 生效");
    CHECK(power_engine_button_target(&e) == CONTROL_TARGET_RUN_SPEED, "跑步多项时优先配速/速度");

    power_engine_set_sport_profile(&e, SPORT_PROFILE_CYCLING);
    CHECK(power_engine_set_broadcast_mask(&e, BROADCAST_FIELD_CADENCE | BROADCAST_FIELD_HEARTRATE), "骑行踏频+心率 mask 生效");
    CHECK(power_engine_button_target(&e) == CONTROL_TARGET_CADENCE, "骑行功率关闭时按钮目标为踏频");
    CHECK(power_engine_set_broadcast_mask(&e, BROADCAST_FIELD_POWER | BROADCAST_FIELD_HEARTRATE), "骑行功率+心率 mask 生效");
    CHECK(power_engine_button_target(&e) == CONTROL_TARGET_POWER, "骑行多项时优先功率");
}

static void test_button_operation_scenario(void)
{
    printf("[test] 模拟按钮操作后功率/踏频/心率输出\n");
    power_engine_t e;
    power_engine_init(&e);
    domain_rng_seed(20260703);
    CHECK(power_engine_set_power(&e, 200), "初始功率设为200W");
    CHECK(e.cadence_base == 90, "初始踏频为90RPM");
    CHECK(power_engine_set_power_step(&e, 25), "按钮步进设为25W");
    power_engine_set_power_float(&e, 0);
    power_engine_set_cadence_float(&e, 0);
    power_engine_clear_dirty(&e);

    const uint32_t short_ms = 500;
    const uint32_t hold_ms = 2000;
    if (button_classify_release(120, short_ms, hold_ms) == BTN_RELEASE_SHORT) {
        power_engine_adjust_power(&e, -e.power_step);
    }
    CHECK(e.base_power == 175, "短按模拟后功率按步进-25W");
    CHECK(power_engine_is_dirty(&e), "短按调功置 dirty");
    power_engine_clear_dirty(&e);

    uint32_t repeat_points[] = {500, 700, 900, 1100, 1900, 2000, 2200};
    int expected = e.base_power;
    for (size_t i = 0; i < sizeof(repeat_points) / sizeof(repeat_points[0]); i++) {
        if (button_repeat_should_adjust(repeat_points[i], hold_ms)) {
            expected += e.power_step;
            power_engine_adjust_power(&e, e.power_step);
        }
    }
    CHECK(e.base_power == expected, "长按自动重复只在进入配网确认前调功");
    CHECK(e.base_power == 300, "500~1900ms 五次重复调功后功率=300W");

    snapshot_t s = power_engine_snapshot(&e);
    CHECK(s.power == e.base_power, "功率浮动0时快照功率等于按钮调功结果");
    CHECK(s.cadence == 90, "踏频浮动0时按钮调功不改变踏频");
    CHECK(s.heartrate >= HR_MIN && s.heartrate <= HR_MAX, "按钮调功后心率保持合法范围");

    CHECK(button_classify_release(2200, short_ms, hold_ms) == BTN_RELEASE_LONG,
          "长按释放进入配网确认动作");
    int before_long_release = e.base_power;
    CHECK(button_repeat_should_adjust(2200, hold_ms) == false,
          "长按确认区间不再继续调功");
    CHECK(e.base_power == before_long_release, "长按确认区间不额外改变功率");
}

static int settle_hr_for_power(power_engine_t *e, int power, int loops)
{
    CHECK(power_engine_set_power(e, power), "随机功率阶段设置功率成功");
    snapshot_t s = { 0 };
    for (int i = 0; i < loops; i++) {
        s = power_engine_snapshot(e);
    }
    return s.heartrate;
}

static void test_random_power_hr_following_scenario(void)
{
    printf("[test] 随机变化功率时心率随动趋势\n");
    power_engine_t e;
    power_engine_init(&e);
    domain_rng_seed(314159);
    profile_set_ftp(&e.profile, 250);
    profile_set_thr_hr(&e.profile, 185);
    power_engine_set_power_float(&e, 0);
    power_engine_set_cadence_float(&e, 0);
    power_engine_clear_user_hr(&e);

    int low_hr = settle_hr_for_power(&e, 80, 900);
    int high_hr = settle_hr_for_power(&e, 420, 1200);
    int recovery_hr = settle_hr_for_power(&e, 120, 1800);

    CHECK(high_hr > low_hr + 20, "高功率阶段心率显著高于低功率阶段");
    CHECK(recovery_hr < high_hr - 8, "功率回落后心率随动下降");
    CHECK(low_hr >= HR_MIN && high_hr <= HR_MAX && recovery_hr >= HR_MIN,
          "随机功率随动阶段心率均在合法范围");

    int sequence[] = {160, 310, 190, 460, 130, 390};
    int min_hr = HR_MAX;
    int max_hr = HR_MIN;
    for (size_t i = 0; i < sizeof(sequence) / sizeof(sequence[0]); i++) {
        int hr = settle_hr_for_power(&e, sequence[i], 260);
        if (hr < min_hr) min_hr = hr;
        if (hr > max_hr) max_hr = hr;
    }
    CHECK(max_hr - min_hr >= 15, "多段随机功率变化产生可观察心率区间");
}

static int settle_hr_for_road_pacer(power_engine_t *e, int wkg10, int loops)
{
    CHECK(profile_set_custom_wkg(&e->profile, wkg10, wkg10), "设置自定义机器人功体比成功");
    if (power_engine_get_pacer_index(e) != PACER_INDEX_CUSTOM) {
        CHECK(power_engine_set_pacer_index(e, PACER_INDEX_CUSTOM), "切换到自定义机器人成功");
    }
    if (e->mode != RIDE_MODE_ROAD) {
        CHECK(power_engine_set_mode(e, RIDE_MODE_ROAD), "切换到配速机器人模式成功");
    }
    snapshot_t s = { 0 };
    for (int i = 0; i < loops; i++) {
        s = power_engine_snapshot(e);
    }
    return s.heartrate;
}

static int settle_hr_for_running_speed(power_engine_t *e, int speed_x100, int loops)
{
    CHECK(power_engine_set_run_speed(e, speed_x100), "设置跑步速度成功");
    running_snapshot_t s = { 0 };
    int hr = 0;
    for (int i = 0; i < loops; i++) {
        s = running_engine_snapshot(&e->running, HR_LOOP_INTERVAL_MS);
        hr = power_engine_running_heartrate(e, s.speed_x100_kmh);
    }
    return hr;
}

static void test_linked_hr_follows_pacer_and_running_speed(void)
{
    printf("[test] 联动心率跟随骑行机器人功率与跑步配速\n");
    power_engine_t e;
    power_engine_init(&e);
    domain_rng_seed(20260707);
    profile_set_weight(&e.profile, 70);
    profile_set_ftp(&e.profile, 250);
    profile_set_thr_hr(&e.profile, 185);
    power_engine_clear_user_hr(&e);

    int easy_road_hr = settle_hr_for_road_pacer(&e, 12, 420);
    int hard_road_hr = settle_hr_for_road_pacer(&e, 55, 620);
    CHECK(hard_road_hr > easy_road_hr + 15, "骑行配速机器人功率提高后联动心率上升");

    power_engine_init(&e);
    domain_rng_seed(20260707);
    profile_set_ftp(&e.profile, 250);
    profile_set_thr_hr(&e.profile, 185);
    power_engine_set_sport_profile(&e, SPORT_PROFILE_RUNNING);
    power_engine_set_run_pace_float(&e, 0);
    power_engine_clear_user_hr(&e);

    int slow_run_hr = settle_hr_for_running_speed(&e, 600, 600);
    int fast_run_hr = settle_hr_for_running_speed(&e, 1600, 1200);
    CHECK(fast_run_hr > slow_run_hr + 10, "跑步配速变快后联动心率上升");

    CHECK(power_engine_set_heartrate(&e, 142), "跑步只控心率/锁定心率时可设置直接心率");
    power_engine_set_hr_float(&e, 0);
    int locked_slow_hr = settle_hr_for_running_speed(&e, 700, 80);
    int locked_fast_hr = settle_hr_for_running_speed(&e, 1800, 80);
    CHECK(locked_slow_hr == 142 && locked_fast_hr == 142,
          "跑步锁定心率时速度变化不影响心率输出");
}

static void test_float_effect_scenario(void)
{
    printf("[test] 浮动数值启停效果\n");
    power_engine_t e;
    power_engine_init(&e);
    domain_rng_seed(271828);
    CHECK(power_engine_set_power(&e, 210), "浮动测试功率设为210W");
    CHECK(power_engine_set_cadence(&e, 88), "浮动测试踏频设为88RPM");
    power_engine_set_heartrate(&e, 150);
    power_engine_set_power_float(&e, 0);
    power_engine_set_cadence_float(&e, 0);
    power_engine_set_hr_float(&e, 0);

    for (int i = 0; i < 80; i++) {
        snapshot_t s = power_engine_snapshot(&e);
        CHECK(s.power == 210, "功率浮动0时恒定");
        CHECK(s.cadence == 88, "踏频浮动0时恒定");
        CHECK(s.heartrate == 150, "固定心率浮动0时恒定");
    }

    power_engine_set_power_float(&e, 30);
    power_engine_set_cadence_float(&e, 10);
    power_engine_set_hr_float(&e, 8);
    power_engine_reset_mode_state(&e);
    int min_power = POWER_MAX, max_power = POWER_MIN;
    int min_cadence = CADENCE_MAX, max_cadence = CADENCE_MIN;
    int min_hr = HR_MAX, max_hr = HR_MIN;
    for (int i = 0; i < 1200; i++) {
        snapshot_t s = power_engine_snapshot(&e);
        CHECK(s.power >= 180 && s.power <= 240, "功率浮动30W时落在210±30");
        CHECK(s.cadence >= 78 && s.cadence <= 98, "踏频浮动10RPM时落在88±10");
        CHECK(s.heartrate >= 142 && s.heartrate <= 158, "心率浮动8BPM时落在150±8");
        if (s.power < min_power) min_power = s.power;
        if (s.power > max_power) max_power = s.power;
        if (s.cadence < min_cadence) min_cadence = s.cadence;
        if (s.cadence > max_cadence) max_cadence = s.cadence;
        if (s.heartrate < min_hr) min_hr = s.heartrate;
        if (s.heartrate > max_hr) max_hr = s.heartrate;
    }
    CHECK(max_power > min_power, "功率浮动开启后输出不恒定");
    CHECK(max_cadence > min_cadence, "踏频浮动开启后输出不恒定");
    CHECK(max_hr > min_hr, "固定心率浮动开启后输出不恒定");
}

/*
 * FIT 中间 5-85 分钟原始 1Hz 参考：190.7±20.7W，功率秒差 P50/P95=3/12W，
 * AC1/AC5=0.955/0.708，功率/踏频相关=0.296；心率最佳滞后约 38 秒，相关 0.352。
 */
static void test_reference_ride_simulation(void)
{
    printf("[test] 默认配置90分钟真实骑行统计仿真\n");
    power_engine_t e;
    power_engine_init(&e);
    domain_rng_seed(20261027);
    CHECK(power_engine_set_power(&e, 190), "仿真基准功率设为190W");
    CHECK(power_engine_set_cadence(&e, 95), "仿真基准踏频设为95RPM");
    CHECK(e.power_float == POWER_FLOAT_DEFAULT && e.power_float == 50,
          "生产默认功率浮动为±50W");
    CHECK(e.cadence_float == CADENCE_FLOAT_DEFAULT && e.cadence_float == 8,
          "生产默认踏频浮动为±8RPM");
    CHECK(fabsf(RIDE_DYNAMICS_FAST_ALPHA - 0.70f) < 0.0001f,
          "功率快层跟随系数使用FIT拟合值0.70");
    CHECK(profile_set_ftp(&e.profile, 250), "仿真FTP设为250W");
    CHECK(profile_set_thr_hr(&e.profile, 180), "仿真阈值心率设为180BPM");
    power_engine_clear_user_hr(&e);
    power_engine_reset_mode_state(&e);

    int min_power = POWER_MAX;
    int max_power = POWER_MIN;
    int min_cadence = CADENCE_MAX;
    int max_cadence = CADENCE_MIN;
    for (int second = 0; second < RIDE_SIM_SECONDS; second++) {
        snapshot_t snapshot = { 0 };
        for (int tick = 0; tick < RIDE_SIM_TICKS_PER_SECOND; tick++) {
            snapshot = power_engine_snapshot(&e);
        }
        s_sim_power[second] = snapshot.power;
        s_sim_cadence[second] = snapshot.cadence;
        s_sim_hr[second] = snapshot.heartrate;
        if (snapshot.power < min_power) min_power = snapshot.power;
        if (snapshot.power > max_power) max_power = snapshot.power;
        if (snapshot.cadence < min_cadence) min_cadence = snapshot.cadence;
        if (snapshot.cadence > max_cadence) max_cadence = snapshot.cadence;
    }

    double power_mean = series_mean(s_sim_power, RIDE_SIM_SECONDS);
    double power_stddev = series_stddev(s_sim_power, RIDE_SIM_SECONDS, power_mean);
    double cadence_mean = series_mean(s_sim_cadence, RIDE_SIM_SECONDS);
    double cadence_stddev = series_stddev(s_sim_cadence, RIDE_SIM_SECONDS, cadence_mean);
    double hr_mean = series_mean(s_sim_hr, RIDE_SIM_SECONDS);
    double hr_stddev = series_stddev(s_sim_hr, RIDE_SIM_SECONDS, hr_mean);
    int power_delta_p50 = absolute_delta_percentile(s_sim_power, RIDE_SIM_SECONDS, 50);
    int power_delta_p95 = absolute_delta_percentile(s_sim_power, RIDE_SIM_SECONDS, 95);
    double power_ac1 = series_correlation(s_sim_power, s_sim_power, RIDE_SIM_SECONDS, 1);
    double power_ac5 = series_correlation(s_sim_power, s_sim_power, RIDE_SIM_SECONDS, 5);
    double power_cadence_corr = series_correlation(s_sim_power, s_sim_cadence,
                                                    RIDE_SIM_SECONDS, 0);
    int hr_delta_p95 = absolute_delta_percentile(s_sim_hr, RIDE_SIM_SECONDS, 95);
    double power_hr_corr_now = series_correlation(s_sim_power, s_sim_hr,
                                                   RIDE_SIM_SECONDS, 0);
    double power_hr_corr_lag30 = series_correlation(s_sim_power, s_sim_hr,
                                                     RIDE_SIM_SECONDS, 30);
    printf("  仿真 P=%.1f±%.1fW ΔP50=%dW ΔP95=%dW AC1=%.3f AC5=%.3f\n",
           power_mean, power_stddev, power_delta_p50, power_delta_p95,
           power_ac1, power_ac5);
    printf("  仿真 P/C相关=%.3f HRΔ95=%dBPM P/HR相关 当前=%.3f 滞后30s=%.3f\n",
           power_cadence_corr, hr_delta_p95, power_hr_corr_now, power_hr_corr_lag30);
    printf("  仿真 C=%.1f±%.1fRPM HR=%.1f±%.1fBPM\n",
           cadence_mean, cadence_stddev, hr_mean, hr_stddev);

    CHECK(min_power >= 140 && max_power <= 240, "90分钟功率始终在190±50W内");
    CHECK(min_cadence >= 87 && max_cadence <= 103, "90分钟踏频始终在95±8RPM内");
    CHECK(power_mean >= 186.0 && power_mean <= 194.0, "长期平均功率保持基准附近");
    CHECK(power_stddev >= 18.0 && power_stddev <= 23.0, "功率离散度接近真实骑行量级");
    CHECK(power_delta_p50 >= 3 && power_delta_p50 <= 5, "功率秒差中位数接近真实骑行");
    CHECK(power_delta_p95 == 12, "固定种子功率秒差95分位命中FIT参考");
    CHECK(power_ac1 >= 0.95 && power_ac1 <= 0.96, "功率秒级连续性命中FIT参考");
    CHECK(power_ac5 >= 0.62 && power_ac5 <= 0.78, "功率具有多秒惯性且不过度平坦");
    CHECK(power_cadence_corr >= 0.18 && power_cadence_corr <= 0.42,
          "踏频与功率保持真实的弱正相关");
    CHECK(cadence_stddev >= 2.3 && cadence_stddev <= 3.6,
          "踏频离散度接近真实骑行量级");
    CHECK(hr_stddev >= 4.0 && hr_stddev <= 6.5,
          "心率离散度接近真实骑行量级");
    CHECK(hr_delta_p95 <= 2, "心率95%秒差不超过2BPM");
    CHECK(power_hr_corr_now >= 0.05 && power_hr_corr_now <= 0.30,
          "功率与心率即时相关不过度耦合");
    CHECK(power_hr_corr_lag30 >= 0.25 && power_hr_corr_lag30 <= 0.50,
          "功率与30秒后心率相关接近真实量级");
    CHECK(power_hr_corr_lag30 > power_hr_corr_now,
          "心率对功率的30秒滞后相关高于即时相关");
}

static void test_reference_ride_multiseed(void)
{
    printf("[test] 默认配置多种子真实骑行统计回归\n");
    double mean_min = 10000.0, mean_max = -10000.0;
    double stddev_min = 10000.0, stddev_max = -10000.0;
    double ac1_min = 10000.0, ac1_max = -10000.0;
    double ac5_min = 10000.0, ac5_max = -10000.0;
    double pc_min = 10000.0, pc_max = -10000.0;
    double phr_min = 10000.0, phr_max = -10000.0;
    int delta95_min = 10000, delta95_max = -10000;

    for (uint32_t seed = 1; seed <= 16; seed++) {
        power_engine_t e;
        power_engine_init(&e);
        domain_rng_seed(seed * 2654435761u);
        power_engine_set_power(&e, 190);
        power_engine_set_cadence(&e, 95);
        profile_set_ftp(&e.profile, 250);
        profile_set_thr_hr(&e.profile, 180);
        power_engine_reset_mode_state(&e);

        for (int second = 0; second < RIDE_SIM_SECONDS; second++) {
            snapshot_t snapshot = { 0 };
            for (int tick = 0; tick < RIDE_SIM_TICKS_PER_SECOND; tick++) {
                snapshot = power_engine_snapshot(&e);
            }
            s_sim_power[second] = snapshot.power;
            s_sim_cadence[second] = snapshot.cadence;
            s_sim_hr[second] = snapshot.heartrate;
        }

        double power_mean = series_mean(s_sim_power, RIDE_SIM_SECONDS);
        double power_stddev = series_stddev(s_sim_power, RIDE_SIM_SECONDS, power_mean);
        int power_delta_p95 = absolute_delta_percentile(s_sim_power, RIDE_SIM_SECONDS, 95);
        double power_ac1 = series_correlation(s_sim_power, s_sim_power,
                                              RIDE_SIM_SECONDS, 1);
        double power_ac5 = series_correlation(s_sim_power, s_sim_power,
                                              RIDE_SIM_SECONDS, 5);
        double power_cadence_corr = series_correlation(s_sim_power, s_sim_cadence,
                                                        RIDE_SIM_SECONDS, 0);
        double power_hr_corr_lag30 = series_correlation(s_sim_power, s_sim_hr,
                                                        RIDE_SIM_SECONDS, 30);

        if (power_mean < mean_min) mean_min = power_mean;
        if (power_mean > mean_max) mean_max = power_mean;
        if (power_stddev < stddev_min) stddev_min = power_stddev;
        if (power_stddev > stddev_max) stddev_max = power_stddev;
        if (power_delta_p95 < delta95_min) delta95_min = power_delta_p95;
        if (power_delta_p95 > delta95_max) delta95_max = power_delta_p95;
        if (power_ac1 < ac1_min) ac1_min = power_ac1;
        if (power_ac1 > ac1_max) ac1_max = power_ac1;
        if (power_ac5 < ac5_min) ac5_min = power_ac5;
        if (power_ac5 > ac5_max) ac5_max = power_ac5;
        if (power_cadence_corr < pc_min) pc_min = power_cadence_corr;
        if (power_cadence_corr > pc_max) pc_max = power_cadence_corr;
        if (power_hr_corr_lag30 < phr_min) phr_min = power_hr_corr_lag30;
        if (power_hr_corr_lag30 > phr_max) phr_max = power_hr_corr_lag30;

        CHECK(power_mean >= 184.0 && power_mean <= 196.0,
              "多种子长期平均功率稳定");
        CHECK(power_stddev >= 17.0 && power_stddev <= 24.0,
              "多种子功率离散度稳定");
        CHECK(power_delta_p95 >= 9 && power_delta_p95 <= 13,
              "多种子功率秒差95分位稳定");
        CHECK(power_ac1 >= 0.93 && power_ac1 <= 0.98,
              "多种子功率AC1稳定");
        CHECK(power_ac5 >= 0.58 && power_ac5 <= 0.80,
              "多种子功率AC5稳定");
        CHECK(power_cadence_corr >= 0.12 && power_cadence_corr <= 0.48,
              "多种子功率踏频弱相关稳定");
        CHECK(power_hr_corr_lag30 >= 0.15 && power_hr_corr_lag30 <= 0.58,
              "多种子功率心率滞后相关稳定");
    }

    printf("  16种子 P均值=%.1f..%.1f SD=%.1f..%.1f ΔP95=%d..%d\n",
           mean_min, mean_max, stddev_min, stddev_max, delta95_min, delta95_max);
    printf("  16种子 AC1=%.3f..%.3f AC5=%.3f..%.3f P/C=%.3f..%.3f P/HR30=%.3f..%.3f\n",
           ac1_min, ac1_max, ac5_min, ac5_max, pc_min, pc_max, phr_min, phr_max);
    CHECK(delta95_max >= 12, "多种子功率秒差P95覆盖FIT的12W量级");
}

static bool snapshot_in_global_range(snapshot_t snapshot)
{
    return snapshot.power >= POWER_MIN && snapshot.power <= POWER_MAX
        && snapshot.cadence >= CADENCE_MIN && snapshot.cadence <= CADENCE_MAX
        && snapshot.heartrate >= HR_MIN && snapshot.heartrate <= HR_MAX
        && snapshot.mode >= RIDE_MODE_STEADY && snapshot.mode < RIDE_MODE_COUNT;
}

static void test_ride_model_edge_cases(void)
{
    printf("[test] 骑行模型极值、退化区间与模式切换稳定性\n");
    power_engine_t e;
    power_engine_init(&e);
    domain_rng_seed(0xA5A55A5Au);

    CHECK(power_engine_set_power(&e, POWER_MIN), "极值测试功率设到下限");
    CHECK(power_engine_set_cadence(&e, CADENCE_MIN), "极值测试踏频设到下限");
    CHECK(power_engine_set_heartrate(&e, HR_MIN), "极值测试心率锁定到下限");
    CHECK(power_engine_set_power_float(&e, POWER_FLOAT_MAX), "功率浮动设到上限");
    CHECK(power_engine_set_cadence_float(&e, CADENCE_FLOAT_MAX), "踏频浮动设到上限");
    CHECK(power_engine_set_hr_float(&e, HR_FLOAT_MAX), "心率浮动设到上限");
    int invalid = 0;
    for (int i = 0; i < 10000; i++) {
        snapshot_t snapshot = power_engine_snapshot(&e);
        if (!snapshot_in_global_range(snapshot)
            || snapshot.power > POWER_MIN + POWER_FLOAT_MAX
            || snapshot.cadence > CADENCE_MIN + CADENCE_FLOAT_MAX
            || snapshot.heartrate > HR_MIN + HR_FLOAT_MAX) {
            invalid++;
        }
    }
    CHECK(invalid == 0, "下边界叠加最大浮动仍不越界");

    CHECK(power_engine_set_power(&e, POWER_MAX), "极值测试功率设到上限");
    CHECK(power_engine_set_cadence(&e, CADENCE_MAX), "极值测试踏频设到上限");
    CHECK(power_engine_set_heartrate(&e, HR_MAX), "极值测试心率锁定到上限");
    power_engine_reset_mode_state(&e);
    invalid = 0;
    for (int i = 0; i < 10000; i++) {
        snapshot_t snapshot = power_engine_snapshot(&e);
        if (!snapshot_in_global_range(snapshot)
            || snapshot.power < POWER_MAX - POWER_FLOAT_MAX
            || snapshot.cadence < CADENCE_MAX - CADENCE_FLOAT_MAX
            || snapshot.heartrate < HR_MAX - HR_FLOAT_MAX) {
            invalid++;
        }
    }
    CHECK(invalid == 0, "上边界叠加最大浮动仍不越界");

    CHECK(power_engine_set_power_float(&e, 0), "功率浮动归零");
    CHECK(power_engine_set_cadence_float(&e, 0), "踏频浮动归零");
    CHECK(power_engine_set_hr_float(&e, 0), "心率浮动归零");
    for (int i = 0; i < 1000; i++) {
        snapshot_t snapshot = power_engine_snapshot(&e);
        if (snapshot.power != POWER_MAX || snapshot.cadence != CADENCE_MAX
            || snapshot.heartrate != HR_MAX) {
            invalid++;
        }
    }
    CHECK(invalid == 0, "全部浮动归零后极值输出恒定");

    power_engine_init(&e);
    CHECK(power_engine_set_cadence_float(&e, 1), "踏频最小非零浮动可设置");
    int min_small_cadence = e.cadence_base;
    int max_small_cadence = e.cadence_base;
    for (int i = 0; i < 10000; i++) {
        snapshot_t snapshot = power_engine_snapshot(&e);
        if (snapshot.cadence < min_small_cadence) min_small_cadence = snapshot.cadence;
        if (snapshot.cadence > max_small_cadence) max_small_cadence = snapshot.cadence;
        if (snapshot.cadence < e.cadence_base - 1
            || snapshot.cadence > e.cadence_base + 1) {
            invalid++;
        }
    }
    CHECK(invalid == 0 && max_small_cadence > min_small_cadence,
          "踏频±1RPM仍有动态且不被耦合预算吞掉");

    power_engine_init(&e);
    domain_rng_seed(0x13572468u);
    CHECK(power_engine_set_pacer_index(&e, PACER_INDEX_CUSTOM), "Road 退化测试切自定义档");
    CHECK(profile_set_custom_wkg(&e.profile, 33, 33), "Road 自定义区间设为单点");
    CHECK(profile_set_weight(&e.profile, 73), "Road 退化测试体重设为73kg");
    CHECK(power_engine_set_mode(&e, RIDE_MODE_ROAD), "Road 退化测试进入机器人模式");
    int degenerate_power = 73 * 33 / 10;
    invalid = 0;
    for (int i = 0; i < 10000; i++) {
        snapshot_t snapshot = power_engine_snapshot(&e);
        if (!snapshot_in_global_range(snapshot) || snapshot.power != degenerate_power) {
            invalid++;
        }
    }
    CHECK(invalid == 0, "Road 单点功体比区间保持确定功率且无除零异常");

    power_engine_init(&e);
    domain_rng_seed(0x24681357u);
    CHECK(power_engine_set_erg_target(&e, POWER_MIN), "ERG 接受0W边界目标");
    invalid = 0;
    for (int i = 0; i < 2000; i++) {
        snapshot_t snapshot = power_engine_snapshot(&e);
        if (snapshot.power < POWER_MIN
            || snapshot.power > POWER_MIN + ERG_POWER_FLOAT) {
            invalid++;
        }
    }
    CHECK(invalid == 0, "ERG 0W目标波动经全局下限钳制");
    CHECK(power_engine_set_erg_target(&e, POWER_MAX), "ERG 接受2000W边界目标");
    for (int i = 0; i < 2000; i++) {
        snapshot_t snapshot = power_engine_snapshot(&e);
        if (snapshot.power < POWER_MAX - ERG_POWER_FLOAT
            || snapshot.power > POWER_MAX) {
            invalid++;
        }
    }
    CHECK(invalid == 0, "ERG 2000W目标波动经全局上限钳制");

    for (int second = 0; second < 3600; second++) {
        int target = (second * 7919) % (POWER_MAX + 1);
        if (!power_engine_set_erg_target(&e, target)) invalid++;
        for (int tick = 0; tick < RIDE_SIM_TICKS_PER_SECOND; tick++) {
            snapshot_t snapshot = power_engine_snapshot(&e);
            int low = target - ERG_POWER_FLOAT;
            int high = target + ERG_POWER_FLOAT;
            if (low < POWER_MIN) low = POWER_MIN;
            if (high > POWER_MAX) high = POWER_MAX;
            if (!snapshot_in_global_range(snapshot) || snapshot.mode != RIDE_MODE_ERG
                || snapshot.power < low || snapshot.power > high) {
                invalid++;
            }
        }
    }
    CHECK(invalid == 0, "ERG 连续一小时每秒换目标仍有界且不污染模式");

    CHECK(power_engine_clear_erg_target(&e), "模式切换测试先退出ERG");
    for (int cycle = 0; cycle < 2000; cycle++) {
        if (e.mode != RIDE_MODE_STEADY) invalid++;
        snapshot_t steady = power_engine_snapshot(&e);
        if (!snapshot_in_global_range(steady) || steady.mode != RIDE_MODE_STEADY) invalid++;
        if (!power_engine_set_mode(&e, RIDE_MODE_ROAD)) invalid++;
        snapshot_t road = power_engine_snapshot(&e);
        if (!snapshot_in_global_range(road) || road.mode != RIDE_MODE_ROAD) invalid++;
        int target = cycle % (POWER_MAX + 1);
        if (!power_engine_set_erg_target(&e, target)) invalid++;
        snapshot_t erg = power_engine_snapshot(&e);
        if (!snapshot_in_global_range(erg) || erg.mode != RIDE_MODE_ERG) invalid++;
        if (!power_engine_clear_erg_target(&e)) invalid++;
    }
    CHECK(invalid == 0, "Steady/Road/ERG 反复切换2000轮状态与输出稳定");
}

static void test_heartrate_lock_unlock_extremes(void)
{
    printf("[test] 心率锁定/解锁极值与重置稳定性\n");
    power_engine_t e;
    power_engine_init(&e);
    domain_rng_seed(0x55AA55AAu);
    CHECK(power_engine_set_hr_float(&e, HR_FLOAT_MAX), "心率极值测试启用最大浮动");
    int invalid = 0;
    for (int cycle = 0; cycle < 500; cycle++) {
        int target = (cycle & 1) ? HR_MAX : HR_MIN;
        if (!power_engine_set_heartrate(&e, target)) invalid++;
        for (int tick = 0; tick < 40; tick++) {
            snapshot_t snapshot = power_engine_snapshot(&e);
            int low = target - HR_FLOAT_MAX;
            int high = target + HR_FLOAT_MAX;
            if (low < HR_MIN) low = HR_MIN;
            if (high > HR_MAX) high = HR_MAX;
            if (snapshot.heartrate < low || snapshot.heartrate > high) invalid++;
        }
        power_engine_clear_user_hr(&e);
        snapshot_t linked = power_engine_snapshot(&e);
        if (!snapshot_in_global_range(linked) || e.has_user_hr) invalid++;
    }
    CHECK(invalid == 0, "心率上下限锁定/解锁500轮无越界或旧锁残留");
}

static void test_ride_model_24h_equivalent(void)
{
    printf("[test] 默认骑行模型24小时等效快照稳定性\n");
    power_engine_t e;
    power_engine_init(&e);
    domain_rng_seed(0xC001D00Du);
    CHECK(power_engine_set_power(&e, 190), "24小时仿真功率设为190W");
    CHECK(power_engine_set_cadence(&e, 95), "24小时仿真踏频设为95RPM");
    CHECK(profile_set_ftp(&e.profile, 250), "24小时仿真FTP设为250W");
    CHECK(profile_set_thr_hr(&e.profile, 180), "24小时仿真阈值心率设为180BPM");
    int invalid = 0;
    int min_power = POWER_MAX;
    int max_power = POWER_MIN;
    int min_cadence = CADENCE_MAX;
    int max_cadence = CADENCE_MIN;
    int min_hr = HR_MAX;
    int max_hr = HR_MIN;
    for (uint64_t tick = 0; tick < LONG_RIDE_SIM_TICKS; tick++) {
        snapshot_t snapshot = power_engine_snapshot(&e);
        if (!snapshot_in_global_range(snapshot)
            || snapshot.power < 190 - POWER_FLOAT_DEFAULT
            || snapshot.power > 190 + POWER_FLOAT_DEFAULT
            || snapshot.cadence < 95 - CADENCE_FLOAT_DEFAULT
            || snapshot.cadence > 95 + CADENCE_FLOAT_DEFAULT) {
            invalid++;
        }
        if (snapshot.power < min_power) min_power = snapshot.power;
        if (snapshot.power > max_power) max_power = snapshot.power;
        if (snapshot.cadence < min_cadence) min_cadence = snapshot.cadence;
        if (snapshot.cadence > max_cadence) max_cadence = snapshot.cadence;
        if (snapshot.heartrate < min_hr) min_hr = snapshot.heartrate;
        if (snapshot.heartrate > max_hr) max_hr = snapshot.heartrate;
    }
    printf("  24h范围 P=%d..%dW C=%d..%dRPM HR=%d..%dBPM 快照=%llu\n",
           min_power, max_power, min_cadence, max_cadence, min_hr, max_hr,
           (unsigned long long)e.snapshot_count);
    CHECK(invalid == 0, "24小时等效运行全程输出有界");
    CHECK(e.snapshot_count == LONG_RIDE_SIM_TICKS, "24小时等效快照计数精确");
    CHECK(max_power > min_power && max_cadence > min_cadence && max_hr > min_hr,
          "24小时等效运行三路数据均保持动态变化");
}

static void test_irregular_notification_sampling(void)
{
    printf("[test] 1Hz通知抖动、延迟丢样与重复保持仿真\n");
    power_engine_t e;
    power_engine_init(&e);
    domain_rng_seed(0x10293847u);
    CHECK(power_engine_set_power(&e, 190), "通知仿真功率设为190W");
    CHECK(power_engine_set_cadence(&e, 95), "通知仿真踏频设为95RPM");
    CHECK(profile_set_ftp(&e.profile, 250), "通知仿真FTP设为250W");
    CHECK(profile_set_thr_hr(&e.profile, 180), "通知仿真阈值心率设为180BPM");

    int received = 0;
    int dropped = 0;
    int duplicated = 0;
    int invalid = 0;
    snapshot_t latest = { 0 };
    for (int slot = 0; slot < RIDE_SIM_SECONDS; slot++) {
        int ticks_until_delivery = 17 + (slot * 37) % 7;
        for (int tick = 0; tick < ticks_until_delivery; tick++) {
            latest = power_engine_snapshot(&e);
        }
        if (slot % 53 == 0) {
            dropped++;
            continue;
        }
        bool duplicate = slot > 0 && received > 0 && slot % 29 == 0;
        if (duplicate) {
            s_sim_power[received] = s_sim_power[received - 1];
            s_sim_cadence[received] = s_sim_cadence[received - 1];
            s_sim_hr[received] = s_sim_hr[received - 1];
            duplicated++;
        } else {
            s_sim_power[received] = latest.power;
            s_sim_cadence[received] = latest.cadence;
            s_sim_hr[received] = latest.heartrate;
        }
        if (!snapshot_in_global_range(latest)) invalid++;
        received++;
    }

    double power_mean = series_mean(s_sim_power, received);
    double power_stddev = series_stddev(s_sim_power, received, power_mean);
    double power_ac1 = series_correlation(s_sim_power, s_sim_power, received, 1);
    int power_delta_p95 = absolute_delta_percentile(s_sim_power, received, 95);
    printf("  接收=%d 丢样=%d 重复=%d P=%.1f±%.1fW ΔP95=%dW AC1=%.3f\n",
           received, dropped, duplicated, power_mean, power_stddev,
           power_delta_p95, power_ac1);
    CHECK(invalid == 0 && received + dropped == RIDE_SIM_SECONDS,
          "不规则采样期间领域快照始终有效且计数守恒");
    CHECK(dropped >= 100 && duplicated >= 180,
          "仿真确实覆盖约2%丢样和约3%重复保持");
    CHECK(power_mean >= 184.0 && power_mean <= 196.0
          && power_stddev >= 17.0 && power_stddev <= 24.0,
          "通知抖动与丢样后功率中心和离散度仍稳定");
    CHECK(power_delta_p95 >= 9 && power_delta_p95 <= 16 && power_ac1 >= 0.90,
          "通知抖动与保持后功率跳变和连续性仍在可接受范围");
}

static void test_web_config_persistence_scenario(void)
{
    printf("[test] 模拟网页端保存配置并持久化往返\n");
    config_data_t saved;
    config_store_defaults(&saved);
    saved.power = 345;
    saved.cadence = 96;
    saved.mode = RIDE_MODE_STEADY;
    saved.heartrate = 166;
    saved.has_user_hr = 1;
    saved.power_float = 18;
    saved.cadence_float = 9;
    saved.hr_float = 7;
    saved.power_step = 35;
    saved.weight = 82;
    saved.ftp = 280;
    saved.thr_hr = 188;
    saved.ride_type = PACER_INDEX_CUSTOM;
    saved.custom_wkg_low = 31;
    saved.custom_wkg_high = 37;
    strcpy(saved.custom_pacer_name, "冲刺档");
    strcpy(saved.device_name, "BikePower-Web");
    saved.led.silent = 1;
    saved.led.scenes[LED_SCENE_WIFI_CONFIG].mode = LED_MODE_BREATH;
    saved.led.scenes[LED_SCENE_WIFI_CONFIG].color = LED_COLOR_CYAN;

    char json[768];
    size_t n = config_store_serialize(&saved, json, sizeof(json));
    CHECK(n > 0, "网页保存配置可序列化");
    CHECK(strstr(json, "\"power\":345") != NULL, "网页保存 JSON 含功率");
    CHECK(strstr(json, "\"has_user_hr\":1") != NULL, "网页保存 JSON 含固定心率锁定");
    CHECK(strstr(json, "\"custom_wkg_low\":31") != NULL, "网页保存 JSON 含自定义 W/kg 下限");
    CHECK(strstr(json, "\"led_silent\":1") != NULL, "网页保存 JSON 含 LED 静默");

    config_data_t loaded;
    CHECK(config_store_deserialize(json, &loaded), "网页保存 JSON 可反序列化");
    CHECK(loaded.power == 345 && loaded.cadence == 96, "网页保存后功率/踏频持久化一致");
    CHECK(loaded.has_user_hr == 1 && loaded.heartrate == 166, "网页保存后固定心率持久化一致");
    CHECK(loaded.power_float == 18 && loaded.cadence_float == 9 && loaded.hr_float == 7,
          "网页保存后浮动幅度持久化一致");
    CHECK(loaded.power_step == 35, "网页保存后按钮步进持久化一致");
    CHECK(loaded.weight == 82 && loaded.ftp == 280 && loaded.thr_hr == 188,
          "网页保存后用户资料持久化一致");
    CHECK(loaded.ride_type == PACER_INDEX_CUSTOM && loaded.custom_wkg_low == 31 && loaded.custom_wkg_high == 37,
          "网页保存后自定义配速档持久化一致");
    CHECK(strcmp(loaded.custom_pacer_name, "冲刺档") == 0, "网页保存后自定义配速昵称持久化一致");
    CHECK(strcmp(loaded.device_name, "BikePower-Web") == 0, "网页保存后设备名持久化一致");
    CHECK(loaded.led.silent == 1, "网页保存后 LED 静默持久化一致");

    power_engine_t e;
    power_engine_init(&e);
    CHECK(power_engine_set_power(&e, loaded.power), "持久化功率可恢复到引擎");
    CHECK(power_engine_set_cadence(&e, loaded.cadence), "持久化踏频可恢复到引擎");
    CHECK(power_engine_set_heartrate(&e, loaded.heartrate), "持久化固定心率可恢复到引擎");
    CHECK(power_engine_set_power_float(&e, loaded.power_float), "持久化功率浮动可恢复到引擎");
    CHECK(power_engine_set_cadence_float(&e, loaded.cadence_float), "持久化踏频浮动可恢复到引擎");
    CHECK(power_engine_set_hr_float(&e, loaded.hr_float), "持久化心率浮动可恢复到引擎");
    CHECK(power_engine_set_power_step(&e, loaded.power_step), "持久化按钮步进可恢复到引擎");
    CHECK(profile_set_weight(&e.profile, loaded.weight), "持久化体重可恢复到引擎");
    CHECK(profile_set_ftp(&e.profile, loaded.ftp), "持久化 FTP 可恢复到引擎");
    CHECK(profile_set_thr_hr(&e.profile, loaded.thr_hr), "持久化阈值心率可恢复到引擎");
    CHECK(power_engine_set_pacer_index(&e, loaded.ride_type), "持久化配速档可恢复到引擎");
    CHECK(profile_set_custom_wkg(&e.profile, loaded.custom_wkg_low, loaded.custom_wkg_high),
          "持久化自定义 W/kg 可恢复到引擎");
    CHECK(profile_set_custom_name(&e.profile, loaded.custom_pacer_name),
          "持久化自定义昵称可恢复到引擎");
    CHECK(power_engine_set_device_name(&e, loaded.device_name), "持久化设备名可恢复到引擎");

    CHECK(e.base_power == 345 && e.cadence_base == 96, "恢复后引擎功率/踏频生效");
    CHECK(e.has_user_hr && e.user_heartrate == 166, "恢复后引擎固定心率生效");
    CHECK(e.power_float == 18 && e.cadence_float == 9 && e.hr_float == 7,
          "恢复后引擎浮动幅度生效");
    CHECK(e.power_step == 35, "恢复后按钮步进生效");
    CHECK(strcmp(power_engine_get_current_pacer(&e), "冲刺档") == 0, "恢复后自定义配速昵称生效");
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

    // set_erg_target 是纯会话态：首次、同值与变更目标都不影响持久配置 dirty。
    power_engine_init(&e);
    int persistent_power = e.base_power;
    CHECK(power_engine_set_erg_target(&e, 200) == true, "首次 ERG 目标成功");
    CHECK(e.mode == RIDE_MODE_ERG && !e.dirty, "ERG 切运行态但不置 dirty");
    CHECK(e.base_power == persistent_power, "首次ERG保留持久base功率");
    CHECK(power_engine_set_erg_target(&e, 200) == true, "同值 ERG 幂等返回 true");
    CHECK(!power_engine_is_dirty(&e), "同值 ERG 不置 dirty");
    CHECK(power_engine_set_erg_target(&e, 210) == true, "变更 ERG 目标成功");
    CHECK(!power_engine_is_dirty(&e), "变更 ERG 不置 dirty");
    CHECK(e.base_power == persistent_power, "变更ERG仍保留持久base功率");

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
    CHECK(strcmp(c.custom_pacer_name, CUSTOM_PACER_NAME_DEFAULT) == 0, "默认自定义配速昵称");
    CHECK(strcmp(c.device_name, DEVICE_NAME_DEFAULT) == 0, "默认设备名");
    CHECK(c.led.scenes[LED_SCENE_WAIT_CONN].mode == LED_MODE_SLOW, "默认待连接慢闪");
    CHECK(c.led.scenes[LED_SCENE_CONNECTED].color == LED_COLOR_GREEN, "默认已连接绿色");
    CHECK(c.led.silent == 0, "默认 LED 非静默");
    CHECK(c.run_pace_float_sec == RUN_PACE_FLOAT_SEC_DEFAULT, "默认跑步配速浮动秒数");
    CHECK(c.run_cadence_float == RUN_CADENCE_FLOAT_DEFAULT, "默认跑步步频浮动");
    CHECK(c.broadcast_mask == BROADCAST_FIELD_ALL, "默认广播项全开");

    // 合法数据不修正
    CHECK(!config_store_validate(&c), "默认值无需校验修正");

    // 空设备名回退默认
    config_data_t d;
    config_store_defaults(&d);
    d.device_name[0] = '\0';
    d.custom_pacer_name[0] = '\0';
    CHECK(config_store_validate(&d), "空设备名被修正");
    CHECK(strcmp(d.device_name, DEVICE_NAME_DEFAULT) == 0, "空设备名回退默认");
    CHECK(strcmp(d.custom_pacer_name, CUSTOM_PACER_NAME_DEFAULT) == 0, "空自定义配速昵称回退默认");

    // 自定义设备名保留
    config_data_t n;
    config_store_defaults(&n);
    strcpy(n.device_name, "MyBike");
    strcpy(n.custom_pacer_name, "兔子车手");
    CHECK(!config_store_validate(&n), "自定义设备名合法不修正");
    CHECK(strcmp(n.device_name, "MyBike") == 0, "自定义设备名保留");
    CHECK(strcmp(n.custom_pacer_name, "兔子车手") == 0, "自定义配速昵称保留");

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
    b.run_pace_float_sec = 999;
    b.run_cadence_float = 999;
    b.broadcast_mask = 0;
    b.led.scenes[LED_SCENE_WAIT_CONN].mode = 99;
    b.led.scenes[LED_SCENE_CONNECTED].color = 99;
    b.led.silent = 9;
    CHECK(config_store_validate(&b), "越界数据被修正");
    CHECK(b.power == POWER_MAX, "功率夹到上限");
    CHECK(b.cadence == CADENCE_MIN, "踏频夹到下限");
    CHECK(b.mode == DEFAULT_RIDE_MODE, "非法模式回默认");
    CHECK(b.heartrate == HR_MAX, "心率夹到上限");
    CHECK(b.weight == PROFILE_WEIGHT_MIN, "体重夹下限");
    CHECK(b.ftp == PROFILE_FTP_MAX, "FTP夹上限");
    CHECK(b.thr_hr == PROFILE_THR_HR_MIN, "阈值心率夹下限");
    CHECK(b.ride_type == PACER_INDEX_DEFAULT, "非法配速机器人回默认");
    CHECK(b.run_pace_float_sec == RUN_PACE_FLOAT_SEC_MAX, "跑步配速浮动夹到上限");
    CHECK(b.run_cadence_float == RUN_CADENCE_FLOAT_MAX, "跑步步频浮动夹到上限");
    CHECK(b.broadcast_mask == BROADCAST_FIELD_ALL, "广播项全关回默认全开");
    CHECK(b.led.scenes[LED_SCENE_WAIT_CONN].mode == LED_MODE_SLOW, "非法 LED 模式回默认慢闪");
    CHECK(b.led.scenes[LED_SCENE_CONNECTED].color == LED_COLOR_BLUE, "非法 LED 颜色回默认蓝色");
    CHECK(b.led.silent == 1, "LED 静默规范化为1");

    // ERG 仅为会话态，旧 NVS 或导入配置中的 ERG 必须修正为 steady 并触发回写。
    config_data_t erg;
    config_store_defaults(&erg);
    erg.mode = RIDE_MODE_ERG;
    erg.power = 333;
    CHECK(config_store_validate(&erg), "持久ERG模式被修正");
    CHECK(erg.mode == RIDE_MODE_STEADY, "持久ERG归一为steady");
    CHECK(erg.power == 333, "持久ERG修正保留base功率");
    CHECK(!config_store_validate(&erg), "ERG修正后二次校验幂等");

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

    config_data_t v6;
    config_store_defaults(&v6);
    v6.schema_ver = 6;
    v6.power = 321;
    v6.run_speed_x100 = 1200;
    v6.run_pace_float_sec = 0;
    CHECK(config_store_migrate(&v6), "v6 配置迁移到 v7");
    CHECK(v6.schema_ver == CONFIG_VERSION, "v6 迁移后版本=当前");
    CHECK(v6.power == 321 && v6.run_speed_x100 == 1200, "v6 迁移保留已有配置");
    CHECK(v6.run_pace_float_sec == RUN_PACE_FLOAT_SEC_DEFAULT, "v6 迁移补默认跑步配速浮动");

    config_data_t v7;
    config_store_defaults(&v7);
    v7.schema_ver = 7;
    v7.power = 322;
    v7.run_cadence = 176;
    v7.run_cadence_float = 0;
    CHECK(config_store_migrate(&v7), "v7 配置迁移到 v8");
    CHECK(v7.schema_ver == CONFIG_VERSION, "v7 迁移后版本=当前");
    CHECK(v7.power == 322 && v7.run_cadence == 176, "v7 迁移保留已有配置");
    CHECK(v7.run_cadence_float == RUN_CADENCE_FLOAT_DEFAULT, "v7 迁移补默认跑步步频浮动");

    config_data_t v8;
    config_store_defaults(&v8);
    v8.schema_ver = 8;
    v8.broadcast_mask = 0;
    CHECK(config_store_migrate(&v8), "v8 配置迁移到 v9");
    CHECK(v8.schema_ver == CONFIG_VERSION, "v8 迁移后版本=当前");
    CHECK(v8.broadcast_mask == BROADCAST_FIELD_ALL, "v8 迁移补默认广播项");

    config_data_t v10;
    config_store_defaults(&v10);
    v10.schema_ver = 10;
    v10.power = 324;
    v10.power_float = 12;
    CHECK(config_store_migrate(&v10), "v10 配置迁移到当前版本");
    CHECK(v10.schema_ver == CONFIG_VERSION, "v10 迁移后版本=当前");
    CHECK(v10.power == 324 && v10.power_float == 12, "v10 迁移保留用户配置");

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
    char js[768];
    config_data_t src;
    config_store_defaults(&src);
    src.power = 222;
    src.cadence = 88;
    src.ftp = 250;
    src.power_float = 12;
    src.cadence_float = 7;
    src.hr_float = 9;
    src.run_pace_float_sec = 12;
    src.run_cadence_float = 6;
    src.run_mode = RUN_MODE_PACER;
    src.run_pacer_type = RUN_PACER_CUSTOM;
    src.run_pace_low_sec = 300;
    src.run_pace_high_sec = 360;
    strcpy(src.run_custom_name, "晨跑\"A");
    src.broadcast_mask = BROADCAST_FIELD_POWER | BROADCAST_FIELD_HEARTRATE;
    src.power_step = 25;
    strcpy(src.custom_pacer_name, "兔子\"车手");
    src.led.silent = 1;
    src.led.scenes[LED_SCENE_CONNECTED].mode = LED_MODE_BREATH;
    src.led.scenes[LED_SCENE_CONNECTED].color = LED_COLOR_MAGENTA;
    src.led.scenes[LED_SCENE_WIFI_CONFIRM].mode = LED_MODE_DOUBLE;
    src.led.scenes[LED_SCENE_WIFI_CONFIRM].color = LED_COLOR_YELLOW;
    strcpy(src.device_name, "MyBike");
    size_t wlen = config_store_serialize(&src, js, sizeof(js));
    CHECK(wlen > 0 && wlen < sizeof(js), "serialize 返回合法长度");
    CHECK(js[wlen] == '\0' && strlen(js) == wlen, "serialize 以 '\\0' 结尾且长度一致");
    CHECK(strstr(js, "\"power\":222") != NULL, "serialize 含 power=222");
    CHECK(strstr(js, "\"power_float\":12") != NULL, "serialize 含 power_float=12");
    CHECK(strstr(js, "\"run_pace_float_sec\":12") != NULL, "serialize 含 run_pace_float_sec=12");
    CHECK(strstr(js, "\"run_cadence_float\":6") != NULL, "serialize 含 run_cadence_float=6");
    CHECK(strstr(js, "\"run_mode\":1") != NULL, "serialize 含 run_mode=1");
    CHECK(strstr(js, "\"run_pacer_type\":4") != NULL, "serialize 含 run_pacer_type=custom");
    CHECK(strstr(js, "\"run_pace_low_sec\":300") != NULL, "serialize 含 run_pace_low_sec=300");
    CHECK(strstr(js, "\"run_pace_high_sec\":360") != NULL, "serialize 含 run_pace_high_sec=360");
    CHECK(strstr(js, "\"run_custom_name\":\"晨跑\\\"A\"") != NULL, "serialize 含转义后的自定义跑步名称");
    CHECK(strstr(js, "\"broadcast_mask\":5") != NULL, "serialize 含 broadcast_mask=5");
    CHECK(strstr(js, "\"power_step\":25") != NULL, "serialize 含 power_step=25");
    CHECK(strstr(js, "\"custom_pacer_name\":\"兔子\\\"车手\"") != NULL, "serialize 含转义后的自定义配速昵称");
    CHECK(strstr(js, "\"led_silent\":1") != NULL, "serialize 含 led_silent=1");
    CHECK(strstr(js, "\"led_conn_mode\":5") != NULL, "serialize 含已连接 LED 模式");
    CHECK(strstr(js, "\"led_confirm_mode\":4") != NULL, "serialize 含确认 LED 模式");
    CHECK(strstr(js, "\"device_name\":\"MyBike\"") != NULL, "serialize 含设备名");

    config_data_t dst;
    CHECK(config_store_deserialize(js, &dst), "deserialize 成功");
    CHECK(dst.power == 222 && dst.cadence == 88 && dst.ftp == 250, "往返整数字段一致");
    CHECK(dst.power_float == 12 && dst.cadence_float == 7 && dst.hr_float == 9, "往返浮动字段一致");
    CHECK(dst.run_pace_float_sec == 12, "往返跑步配速浮动一致");
    CHECK(dst.run_cadence_float == 6, "往返跑步步频浮动一致");
    CHECK(dst.run_mode == RUN_MODE_PACER && dst.run_pacer_type == RUN_PACER_CUSTOM, "往返跑步模式/模板一致");
    CHECK(dst.run_pace_low_sec == 300 && dst.run_pace_high_sec == 360, "往返跑步配速区间一致");
    CHECK(strcmp(dst.run_custom_name, "晨跑\"A") == 0, "往返自定义跑步名称一致");
    CHECK(dst.broadcast_mask == (BROADCAST_FIELD_POWER | BROADCAST_FIELD_HEARTRATE), "往返广播项一致");
    CHECK(dst.power_step == 25, "往返调功步进一致");
    CHECK(strcmp(dst.custom_pacer_name, "兔子\"车手") == 0, "往返自定义配速昵称一致");
    CHECK(dst.led.silent == 1, "往返 LED 静默一致");
    CHECK(dst.led.scenes[LED_SCENE_CONNECTED].mode == LED_MODE_BREATH, "往返已连接 LED 模式一致");
    CHECK(dst.led.scenes[LED_SCENE_CONNECTED].color == LED_COLOR_MAGENTA, "往返已连接 LED 颜色一致");
    CHECK(dst.led.scenes[LED_SCENE_WIFI_CONFIRM].mode == LED_MODE_DOUBLE, "往返确认 LED 模式一致");
    CHECK(strcmp(dst.device_name, "MyBike") == 0, "往返设备名一致");
    CHECK(dst.schema_ver == src.schema_ver && dst.mode == src.mode, "往返其余字段一致");

    // 缺键：仅覆盖出现的字段，其余保持默认
    config_data_t partial;
    CHECK(config_store_deserialize("{\"schema_ver\":5,\"power\":300}", &partial),
          "部分键 JSON deserialize 成功");
    CHECK(partial.power == 300, "出现的 power 被覆盖");
    CHECK(partial.cadence == DEFAULT_CADENCE, "缺失的 cadence 保持默认");
    CHECK(strcmp(partial.custom_pacer_name, CUSTOM_PACER_NAME_DEFAULT) == 0, "缺失自定义配速昵称保持默认");
    CHECK(partial.led.scenes[LED_SCENE_CONNECTED].mode == LED_MODE_SOLID, "缺失 LED 保持默认");
    CHECK(strcmp(partial.device_name, DEVICE_NAME_DEFAULT) == 0, "缺失设备名保持默认");

    config_data_t legacy_run;
    CHECK(config_store_deserialize("{\"schema_ver\":9,\"run_cadence\":180}", &legacy_run),
          "旧跑步 JSON deserialize 成功");
    CHECK(legacy_run.run_cadence == 180, "旧 JSON 中仍保留有效跑步步频字段");

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
    const char *json = "{\"version\":\"3.1.8\",\"url\":\"https://github.com/x/releases/f.bin\",\"size\":102400,\"changelog\":\"修复 OTA\"}";
    char out[128];
    int n;

    CHECK(json_get_str(json, "version", out, sizeof(out)), "提取 version");
    CHECK(strcmp(out, "3.1.8") == 0, "version 值正确");

    CHECK(json_get_str(json, "url", out, sizeof(out)), "提取 url");
    CHECK(strcmp(out, "https://github.com/x/releases/f.bin") == 0, "url 值正确");

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

static void test_device_templates(void)
{
    printf("[test] 设备信息模板 默认/保存/删除/序列化\n");
    device_template_list_t list;
    device_template_list_defaults(&list);
    CHECK(list.count == 0, "默认模板数量为0");
    for (int i = 0; i < DEVICE_TEMPLATE_MAX; i++) {
        CHECK(list.items[i].used == false, "默认模板槽位未使用");
    }

    device_template_t tpl;
    device_template_defaults(&tpl);
    tpl.used = true;
    tpl.id = 9;
    strcpy(tpl.alias, "迈金功率计");
    strcpy(tpl.adv_name, "Magene P325");
    strcpy(tpl.device_name, "P325");
    strcpy(tpl.manufacturer, "Magene");
    strcpy(tpl.model, "P325-CS");
    strcpy(tpl.firmware, "1.2.8");
    strcpy(tpl.addr, "AA:BB:CC:DD:EE:FF");
    strcpy(tpl.adv_raw_hex, "02010603031818");
    strcpy(tpl.scan_rsp_raw_hex, "09094d6167656e65");
    strcpy(tpl.manufacturer_data_hex, "ffff0102");
    strcpy(tpl.service_data_hex, "1818b900");
    tpl.addr_type = 1;
    tpl.last_rssi = -58;
    tpl.tx_power = -4;
    tpl.appearance = 1152;
    tpl.service_flags = DEVICE_TEMPLATE_SVC_CPS | DEVICE_TEMPLATE_SVC_DIS;

    CHECK(device_template_list_add(&list, &tpl) == 0, "新增模板写入第0槽");
    CHECK(list.count == 1, "新增后模板数量=1");
    CHECK(list.items[0].used == true, "第0槽已使用");
    CHECK(list.items[0].id == 0, "新增时重编号为槽位ID");
    CHECK(strcmp(list.items[0].alias, "迈金功率计") == 0, "模板别名保留");
    CHECK(strcmp(list.items[0].device_name, "P325") == 0, "模板设备名保留");
    CHECK(strcmp(list.items[0].adv_raw_hex, "02010603031818") == 0, "模板广播原始 hex 保留");
    CHECK(list.items[0].tx_power == -4, "模板 TX Power 保留");
    CHECK(list.items[0].service_flags == (DEVICE_TEMPLATE_SVC_CPS | DEVICE_TEMPLATE_SVC_DIS), "服务标记保留");

    device_template_t tpl_same_addr;
    device_template_defaults(&tpl_same_addr);
    tpl_same_addr.used = true;
    strcpy(tpl_same_addr.alias, "更新别名");
    strcpy(tpl_same_addr.device_name, "P325");
    strcpy(tpl_same_addr.addr, "AA:BB:CC:DD:EE:FF");
    tpl_same_addr.addr_type = 1;
    strcpy(tpl_same_addr.firmware, "1.2.9");
    tpl_same_addr.service_flags = DEVICE_TEMPLATE_SVC_CSC;
    CHECK(device_template_list_add(&list, &tpl_same_addr) == 0, "相同 MAC 地址模板更新原槽位");
    CHECK(list.count == 1, "相同 MAC 地址不新增重复模板");
    CHECK(strcmp(list.items[0].alias, "更新别名") == 0, "相同 MAC 地址保存时更新别名");
    CHECK(strcmp(list.items[0].firmware, "1.2.9") == 0, "相同 MAC 地址保存时更新版本信息");
    CHECK(list.items[0].service_flags == (DEVICE_TEMPLATE_SVC_CPS | DEVICE_TEMPLATE_SVC_DIS | DEVICE_TEMPLATE_SVC_CSC), "相同 MAC 地址合并服务标记");

    device_template_t tpl_same_name;
    device_template_defaults(&tpl_same_name);
    tpl_same_name.used = true;
    strcpy(tpl_same_name.alias, "同名功率计");
    strcpy(tpl_same_name.device_name, "P325");
    strcpy(tpl_same_name.addr, "10:20:30:40:50:60");
    tpl_same_name.addr_type = 0;
    strcpy(tpl_same_name.manufacturer, "MagenePlus");
    CHECK(device_template_list_add(&list, &tpl_same_name) == 0, "相同设备名称模板更新原槽位");
    CHECK(list.count == 1, "相同设备名称不新增重复模板");
    CHECK(strcmp(list.items[0].manufacturer, "MagenePlus") == 0, "相同设备名称保存时更新厂商信息");

    for (int i = 1; i < DEVICE_TEMPLATE_MAX; i++) {
        device_template_t extra;
        device_template_defaults(&extra);
        extra.used = true;
        snprintf(extra.alias, sizeof(extra.alias), "模板%d", i);
        snprintf(extra.device_name, sizeof(extra.device_name), "Bike%d", i);
        CHECK(device_template_list_add(&list, &extra) == i, "模板追加到下一个空槽");
    }
    CHECK(list.count == DEVICE_TEMPLATE_MAX, "模板数量达到上限5");
    device_template_t overflow;
    device_template_defaults(&overflow);
    overflow.used = true;
    strcpy(overflow.alias, "溢出模板");
    strcpy(overflow.device_name, "OverflowBike");
    strcpy(overflow.addr, "66:55:44:33:22:11");
    CHECK(device_template_list_add(&list, &overflow) == -1, "超过5个模板拒绝保存");

    CHECK(device_template_list_delete(&list, 2), "删除第2槽成功");
    CHECK(list.count == DEVICE_TEMPLATE_MAX - 1, "删除后数量减少");
    CHECK(list.items[2].used == false, "删除后槽位清空");
    CHECK(!device_template_list_delete(&list, 9), "非法ID删除失败");

    device_template_t bad;
    device_template_defaults(&bad);
    bad.used = true;
    bad.addr_type = 99;
    bad.last_rssi = 30;
    memset(bad.alias, 'A', sizeof(bad.alias));
    memset(bad.device_name, 'B', sizeof(bad.device_name));
    bad.alias[sizeof(bad.alias) - 1] = '\0';
    bad.device_name[sizeof(bad.device_name) - 1] = '\0';
    CHECK(device_template_validate(&bad), "非法地址类型/RSSI 被修正");
    CHECK(bad.addr_type == 0, "非法地址类型回退0");
    CHECK(bad.last_rssi == 0, "正RSSI回退0");
    CHECK(bad.alias[DEVICE_TEMPLATE_ALIAS_MAX - 1] == '\0', "别名保证结尾");
    CHECK(bad.device_name[DEVICE_TEMPLATE_NAME_MAX - 1] == '\0', "设备名保证结尾");

    device_template_t scanned;
    device_template_defaults(&scanned);
    uint8_t addr[6] = {0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa};
    CHECK(device_template_set_addr(&scanned, addr, 1), "扫描地址写入成功");
    CHECK(strcmp(scanned.addr, "AA:BB:CC:DD:EE:FF") == 0, "BLE 地址按常见大端展示");
    CHECK(scanned.addr_type == 1, "地址类型写入成功");
    CHECK(device_template_note_service(&scanned, 0x180A), "记录 DIS 服务成功");
    CHECK(device_template_note_service(&scanned, 0x1818), "记录 CPS 服务成功");
    CHECK(!device_template_note_service(&scanned, 0xFFFF), "未知服务不改变模板");
    CHECK(scanned.service_flags == (DEVICE_TEMPLATE_SVC_DIS | DEVICE_TEMPLATE_SVC_CPS), "服务 UUID 映射为模板标记");
    CHECK(device_template_has_ride_service(DEVICE_TEMPLATE_SVC_CPS), "CPS 识别为骑行服务");
    CHECK(device_template_has_ride_service(DEVICE_TEMPLATE_SVC_CSC), "CSC 识别为骑行服务");
    CHECK(device_template_has_ride_service(DEVICE_TEMPLATE_SVC_HRS), "HRS 识别为骑行服务");
    CHECK(device_template_has_ride_service(DEVICE_TEMPLATE_SVC_FTMS), "FTMS 识别为骑行服务");
    CHECK(device_template_has_ride_service(DEVICE_TEMPLATE_SVC_DIS | DEVICE_TEMPLATE_SVC_BAS) == false, "仅设备信息/电量不算骑行服务");

    char json[2048];
    size_t n = device_template_list_serialize_json(&list, json, sizeof(json));
    CHECK(n > 0 && n < sizeof(json), "模板列表 JSON 序列化成功");
    CHECK(strstr(json, "\"max\":5") != NULL, "JSON 含 max=5");
    CHECK(strstr(json, "\"count\":4") != NULL, "JSON 含 count=4");
    CHECK(strstr(json, "\"alias\":\"同名功率计\"") != NULL, "JSON 含更新后的中文别名");
    CHECK(strstr(json, "\"manufacturer\":\"MagenePlus\"") != NULL, "JSON 含更新后的厂商");
    CHECK(strstr(json, "\"service_flags\":21") != NULL, "JSON 含合并后的服务标记");
    CHECK(strstr(json, "\"adv_raw_hex\":\"02010603031818\"") != NULL, "JSON 含广播原始 hex");
    CHECK(strstr(json, "\"scan_rsp_raw_hex\":\"09094d6167656e65\"") != NULL, "JSON 含扫描响应原始 hex");
    CHECK(strstr(json, "\"manufacturer_data_hex\":\"ffff0102\"") != NULL, "JSON 含厂商数据原始 hex");
    CHECK(strstr(json, "\"service_data_hex\":\"1818b900\"") != NULL, "JSON 含服务数据原始 hex");
    CHECK(strstr(json, "\"tx_power\":-4") != NULL, "JSON 含 TX Power");
    CHECK(strstr(json, "\"appearance\":1152") != NULL, "JSON 含 Appearance");
    CHECK(device_template_list_serialize_json(&list, json, 8) == 0, "JSON 缓冲不足返回0");
}

static void test_device_analysis(void)
{
    printf("[test] 外部运动设备分析解析\n");
    device_analysis_t a;
    device_analysis_defaults(&a);
    CHECK(strcmp(a.status, "未采集") == 0, "分析默认状态为未采集");

    const uint8_t cps[] = {0x20, 0x00, 0xb9, 0x00, 0x12, 0x00, 0x40, 0x12};
    CHECK(device_analysis_parse_cps(&a, cps, sizeof(cps)), "解析功率样本");
    CHECK(a.power_watts == 185, "功率样本提取瓦特");
    CHECK((a.service_flags & DEVICE_TEMPLATE_SVC_CPS) != 0, "功率样本记录服务能力");
    CHECK(strcmp(a.last_cps_hex, "2000b90012004012") == 0, "记录功率原始通知 hex");

    const uint8_t hr8[] = {0x00, 145};
    CHECK(device_analysis_parse_hr(&a, hr8, sizeof(hr8)), "解析8位心率样本");
    CHECK(a.heart_rate_bpm == 145, "8位心率提取成功");
    CHECK(strcmp(a.last_hrs_hex, "0091") == 0, "记录心率原始通知 hex");

    const uint8_t csc[] = {0x02, 0x31, 0x00, 0x80, 0x10};
    CHECK(device_analysis_parse_csc(&a, csc, sizeof(csc)), "解析踏频样本");
    CHECK(a.cadence_rpm == 1, "踏频样本识别为有曲柄数据");
    CHECK(strcmp(a.last_csc_hex, "0231008010") == 0, "记录踏频原始通知 hex");

    const uint8_t feature[] = {0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00};
    CHECK(device_analysis_parse_ftms_feature(&a, feature, sizeof(feature)), "解析智能骑行台能力");
    CHECK(a.ftms_control_supported == 1, "目标控制能力识别成功");
    CHECK(strcmp(a.ftms_feature_hex, "0000000020000000") == 0, "记录智能骑行台能力原始 hex");

    const uint8_t ftms[] = {0x44, 0x00, 0x1c, 0x0b, 0xa4, 0x00, 0xbe, 0x00};
    CHECK(device_analysis_parse_ftms_bike(&a, ftms, sizeof(ftms)), "解析智能骑行台数据");
    CHECK(a.speed_x10_kmh == 284, "速度字段按0.1km/h提取");
    CHECK(a.cadence_rpm == 82, "踏频字段按0.5rpm换算");
    CHECK(a.power_watts == 190, "智能骑行台功率提取");
    CHECK(strcmp(a.last_ftms_hex, "44001c0ba400be00") == 0, "记录智能骑行台数据原始通知 hex");

    device_analysis_finalize(&a);
    CHECK(strcmp(a.status, "数据正常") == 0, "收到样本后状态正常");
    CHECK(strstr(a.summary, "功率 190W") != NULL, "摘要包含功率");
    CHECK(strstr(a.summary, "踏频 82rpm") != NULL, "摘要包含踏频");
    CHECK(strstr(a.summary, "心率 145") != NULL, "摘要包含心率");
    CHECK(strstr(a.summary, "速度 28.4km/h") != NULL, "摘要包含速度");

    char json[512];
    size_t n = device_analysis_serialize_json(&a, json, sizeof(json));
    CHECK(n > 0 && n < sizeof(json), "分析结果 JSON 序列化成功");
    CHECK(strstr(json, "\"sample_count\":4") != NULL, "JSON 含样本数量");
    CHECK(strstr(json, "\"ftms_control_supported\":true") != NULL, "JSON 含控制能力");
    CHECK(strstr(json, "\"last_cps_hex\":\"2000b90012004012\"") != NULL, "JSON 含功率原始通知");
    CHECK(strstr(json, "\"last_hrs_hex\":\"0091\"") != NULL, "JSON 含心率原始通知");
    CHECK(strstr(json, "\"last_csc_hex\":\"0231008010\"") != NULL, "JSON 含踏频原始通知");
    CHECK(strstr(json, "\"last_ftms_hex\":\"44001c0ba400be00\"") != NULL, "JSON 含智能骑行台原始通知");
    CHECK(strstr(json, "\"ftms_feature_hex\":\"0000000020000000\"") != NULL, "JSON 含智能骑行台能力原始数据");

    device_analysis_t empty;
    device_analysis_defaults(&empty);
    device_analysis_finalize(&empty);
    CHECK(strcmp(empty.status, "未收到数据") == 0, "无样本状态为未收到数据");
    CHECK(strstr(empty.summary, "未收到运动数据") != NULL, "无样本摘要可读");
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
    printf("[test] LED 灯效配置/决策\n");
    const uint8_t B = 0x30;
    uint8_t rgb[3];
    led_config_t cfg;
    led_config_defaults(&cfg);

    CHECK(cfg.scenes[LED_SCENE_WAIT_CONN].mode == LED_MODE_SLOW, "默认待连接慢闪");
    CHECK(cfg.scenes[LED_SCENE_CONNECTED].mode == LED_MODE_SOLID, "默认已连接常亮");
    CHECK(cfg.scenes[LED_SCENE_WIFI_CONFIG].color == LED_COLOR_CYAN, "默认配网青色");

    led_effect_t e = led_resolve(LED_SCENE_CONNECTED, &cfg, true);
    CHECK(e.mode == LED_MODE_SOLID && e.color == LED_COLOR_GREEN, "已连接场景解析默认常亮绿色");

    cfg.silent = 1;
    e = led_resolve(LED_SCENE_CONNECTED, &cfg, true);
    CHECK(e.mode == LED_MODE_OFF, "静默时非异常场景灭灯");
    cfg.scenes[LED_SCENE_ERROR].mode = LED_MODE_OFF;
    e = led_resolve(LED_SCENE_ERROR, &cfg, true);
    CHECK(e.mode == LED_MODE_OFF, "静默时异常场景也灭灯");

    cfg.silent = 0;
    e = led_resolve(LED_SCENE_ERROR, &cfg, true);
    CHECK(e.mode == LED_MODE_FAST && e.color == LED_COLOR_RED, "非静默时异常场景灭灯回退快闪");
    cfg.scenes[LED_SCENE_CONNECTED].mode = LED_MODE_BREATH;
    e = led_resolve(LED_SCENE_CONNECTED, &cfg, false);
    CHECK(e.mode == LED_MODE_SLOW, "非 RGB 板呼吸降级为慢闪");

    cfg.scenes[LED_SCENE_WAIT_CONN].mode = 99;
    cfg.scenes[LED_SCENE_WAIT_CONN].color = 99;
    cfg.silent = 9;
    CHECK(led_config_validate(&cfg), "LED 配置越界被修正");
    CHECK(cfg.scenes[LED_SCENE_WAIT_CONN].mode == LED_MODE_SLOW, "LED 模式越界回慢闪");
    CHECK(cfg.scenes[LED_SCENE_WAIT_CONN].color == LED_COLOR_BLUE, "LED 颜色越界回蓝色");
    CHECK(cfg.silent == 1, "LED 静默越界规范化");

    led_color_to_rgb(LED_COLOR_RED, B, rgb);
    CHECK(rgb[0] == 0 && rgb[1] == B && rgb[2] == 0, "红色转 GRB");

    led_color_to_rgb(LED_COLOR_GREEN, B, rgb);
    CHECK(rgb[0] == B && rgb[1] == 0 && rgb[2] == 0, "绿色转 GRB");

    led_color_to_rgb(LED_COLOR_BLUE, B, rgb);
    CHECK(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == B, "蓝色转 GRB");

    led_color_to_rgb(LED_COLOR_WHITE, 0x10, rgb);
    CHECK(rgb[0] == 0x10 && rgb[1] == 0x10 && rgb[2] == 0x10, "白色亮度生效");

    CHECK(led_mode_period_ms(LED_MODE_SLOW, 1000, 200) == 2000, "慢闪周期");
    CHECK(led_mode_period_ms(LED_MODE_FAST, 1000, 200) == 400, "快闪周期");
    CHECK(led_mode_scale(LED_MODE_OFF, 0, 1000, 200) == 0, "OFF 亮度 0");
    CHECK(led_mode_scale(LED_MODE_SOLID, 0, 1000, 200) == 255, "常亮亮度 255");
    CHECK(led_mode_scale(LED_MODE_SLOW, 999, 1000, 200) == 255, "慢闪前半亮");
    CHECK(led_mode_scale(LED_MODE_SLOW, 1000, 1000, 200) == 0, "慢闪后半灭");
    CHECK(led_mode_scale(LED_MODE_FAST, 199, 1000, 200) == 255, "快闪前半亮");
    CHECK(led_mode_scale(LED_MODE_FAST, 200, 1000, 200) == 0, "快闪后半灭");
    CHECK(led_mode_scale(LED_MODE_DOUBLE, 0, 1000, 200) == 255, "双闪第一下亮");
    CHECK(led_mode_scale(LED_MODE_DOUBLE, 120, 1000, 200) == 0, "双闪间隔灭");
    CHECK(led_mode_scale(LED_MODE_DOUBLE, 300, 1000, 200) == 255, "双闪第二下亮");
    CHECK(led_mode_scale(LED_MODE_BREATH, 0, 1000, 200) == 0, "呼吸起点灭");
    CHECK(led_mode_scale(LED_MODE_BREATH, 1000, 1000, 200) == 255, "呼吸半周期最亮");
    CHECK(led_mode_scale(LED_MODE_BREATH, 2000, 1000, 200) == 0, "呼吸周期末灭");
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

static void test_running_engine_defaults(void)
{
    printf("[test] 跑步模式默认快照与 RSC 单位换算\n");
    running_engine_t e;
    running_engine_init(&e);
    CHECK(e.running, "跑步引擎默认处于运行态");

    running_snapshot_t first = running_engine_snapshot(&e, 1000);
    CHECK(first.running, "默认跑步快照标记运行中");
    CHECK(first.speed_x100_kmh == RUN_SPEED_X100_DEFAULT, "默认跑步速度=10.0km/h");
    CHECK(first.cadence_spm == RUN_CADENCE_DEFAULT, "默认跑步步频=170spm");
    CHECK(first.rsc_speed_1_256_mps == 711, "RSC速度单位=1/256m/s");
    CHECK(first.stride_cm == 98, "默认步幅约98cm");
    CHECK(first.total_distance_m == 2, "1秒后累计距离整数米=2");

    running_snapshot_t second = running_engine_snapshot(&e, 1000);
    CHECK(second.total_distance_m == 5, "2秒后累计距离整数米=5");
    CHECK(second.elapsed_time_s == 2, "2秒后用时=2s");

    CHECK(e.pace_float_sec == RUN_PACE_FLOAT_SEC_DEFAULT, "默认跑步配速浮动=10秒/km");
    CHECK(running_engine_set_pace_float(&e, 0), "关闭跑步配速浮动");
    CHECK(running_engine_set_pace_float(&e, 10), "设置跑步配速浮动");
    CHECK(e.pace_float_sec == 10, "跑步配速浮动写入运行时");
    domain_rng_seed(42);
    bool saw_changed_speed = false;
    for (int i = 0; i < 180; i++) {
        running_snapshot_t s = running_engine_snapshot(&e, HR_LOOP_INTERVAL_MS);
        CHECK(s.speed_x100_kmh >= 968 && s.speed_x100_kmh <= 1034, "配速浮动速度保持在 5:00±10s 区间");
        if (s.speed_x100_kmh != RUN_SPEED_X100_DEFAULT) saw_changed_speed = true;
    }
    CHECK(saw_changed_speed, "配速浮动会改变对外跑步速度");
}

static int pace_sec_from_speed_x100(int speed_x100)
{
    return speed_x100 > 0 ? (360000 + speed_x100 / 2) / speed_x100 : 0;
}

static void test_running_pacer_defaults(void)
{
    printf("[test] 跑步模板默认配置\n");
    config_data_t c;
    config_store_defaults(&c);

    CHECK(CONFIG_VERSION == 12, "配置 schema 版本升级到 12");
    CHECK(c.run_mode == RUN_MODE_FIXED, "默认跑步模式为固定配速");
    CHECK(c.run_pacer_type == RUN_PACER_JOG, "默认跑步模板为慢跑");
    CHECK(c.run_pace_low_sec == 390, "默认跑步模板下限=6:30/km");
    CHECK(c.run_pace_high_sec == 450, "默认跑步模板上限=7:30/km");
    CHECK(strcmp(c.run_custom_name, RUN_CUSTOM_NAME_DEFAULT) == 0, "默认自定义跑步档名称");
}

static void test_running_pacer_templates(void)
{
    printf("[test] 跑步模板区间输出\n");
    running_engine_t e;
    running_engine_init(&e);
    domain_rng_seed(20260710);

    CHECK(running_engine_set_pacer(&e, RUN_PACER_AEROBIC), "切换有氧模板成功");
    CHECK(e.mode == RUN_MODE_PACER, "模板切换后进入模板模式");
    CHECK(e.pace_low_sec == 330 && e.pace_high_sec == 390, "有氧模板配速区间=5:30-6:30/km");

    CHECK(running_engine_set_pacer(&e, RUN_PACER_TEMPO), "切换节奏模板成功");
    CHECK(e.pace_low_sec == 270 && e.pace_high_sec == 315, "节奏模板配速区间=4:30-5:15/km");

    CHECK(running_engine_set_pacer(&e, RUN_PACER_INTERVAL), "切换间歇模板成功");
    CHECK(e.pace_low_sec == 210 && e.pace_high_sec == 255, "间歇模板配速区间=3:30-4:15/km");

    bool saw_motion = false;
    int previous_speed = 0;
    for (int i = 0; i < 200; i++) {
        running_snapshot_t s = running_engine_snapshot(&e, 1000);
        int pace = pace_sec_from_speed_x100(s.speed_x100_kmh);
        CHECK(pace >= 210 && pace <= 255, "间歇模板输出配速保持在区间内");
        if (previous_speed != 0 && s.speed_x100_kmh != previous_speed) saw_motion = true;
        previous_speed = s.speed_x100_kmh;
    }
    CHECK(saw_motion, "模板模式配速会平滑变化");
}

static void test_running_custom_pacer_validation(void)
{
    printf("[test] 跑步自定义模板校验\n");
    running_engine_t e;
    running_engine_init(&e);

    CHECK(running_engine_set_custom_pacer(&e, "晨跑", 300, 360, 176), "设置自定义模板成功");
    CHECK(e.mode == RUN_MODE_PACER && e.pacer_type == RUN_PACER_CUSTOM, "自定义模板进入模板模式");
    CHECK(e.pace_low_sec == 300 && e.pace_high_sec == 360, "自定义模板区间保留合法值");
    CHECK(e.cadence_spm == 176, "自定义模板步频生效");
    CHECK(strcmp(e.custom_name, "晨跑") == 0, "自定义模板名称生效");

    CHECK(running_engine_set_custom_pacer(&e, "", 1, 9999, 999), "越界自定义模板被夹紧");
    CHECK(e.pace_low_sec == RUN_PACE_SEC_MIN, "自定义模板下限夹到最小配速");
    CHECK(e.pace_high_sec == RUN_PACE_SEC_MAX, "自定义模板上限夹到最大配速");
    CHECK(e.cadence_spm == RUN_CADENCE_MAX, "自定义模板步频夹到上限");
    CHECK(strcmp(e.custom_name, RUN_CUSTOM_NAME_DEFAULT) == 0, "空自定义名称回退默认");

    CHECK(running_engine_set_custom_pacer(&e, "反向", 500, 400, 170), "反向区间自定义模板可校验");
    CHECK(e.pace_low_sec == 500 && e.pace_high_sec == 500, "自定义模板下限大于上限时上限跟随下限");
}

static void test_running_control_values(void)
{
    printf("[test] 跑步控制值\n");
    power_engine_t e;
    power_engine_init(&e);
    CHECK(power_engine_set_sport_profile(&e, SPORT_PROFILE_RUNNING), "切到跑步 profile");

    CHECK(power_engine_set_run_pacer(&e, RUN_PACER_CUSTOM), "切到跑步自定义模板");
    CHECK(e.running.mode == RUN_MODE_PACER, "跑步自定义模板模式生效");
    CHECK(power_engine_set_run_speed(&e, 1234), "控制点速度设置成功");
    CHECK(e.running.mode == RUN_MODE_FIXED, "设置跑步速度后切回固定配速");
    CHECK(e.running.speed_x100_kmh == 1234, "控制点速度值生效");
    CHECK(power_engine_set_run_pacer(&e, RUN_PACER_CUSTOM), "再次切到自定义模板");
    CHECK(power_engine_button_target(&e) == CONTROL_TARGET_RUN_SPEED, "跑步按钮仍优先控制配速");

    running_snapshot_t before_reset = running_engine_snapshot(&e.running, 5000);
    CHECK(before_reset.total_distance_m > 0 && before_reset.elapsed_time_s > 0, "跑步累计距离和用时已推进");
    power_engine_clear_dirty(&e);
    power_engine_run_reset(&e);
    running_snapshot_t after_reset = running_engine_snapshot(&e.running, 0);
    CHECK(after_reset.total_distance_m == 0 && after_reset.elapsed_time_s == 0, "跑步 reset 清空距离和用时");
    CHECK(!power_engine_is_dirty(&e), "跑步 reset 不置持久配置dirty");
}

static void test_running_session_state(void)
{
    printf("[test] 跑步会话暂停、恢复与reset语义\n");
    power_engine_t e;
    power_engine_init(&e);
    CHECK(power_engine_set_sport_profile(&e, SPORT_PROFILE_RUNNING), "切换到跑步profile");
    power_engine_clear_dirty(&e);

    running_snapshot_t active = running_engine_snapshot(&e.running, 2000);
    CHECK(active.running && active.speed_x100_kmh > 0 && active.cadence_spm > 0, "运行快照输出速度和步频");
    CHECK(active.total_distance_m > 0 && active.elapsed_time_s == 2, "运行快照推进累计");

    CHECK(power_engine_set_run_running(&e, false), "Stop切换到暂停态");
    CHECK(!power_engine_is_dirty(&e), "Stop不置dirty");
    running_snapshot_t paused1 = running_engine_snapshot(&e.running, 5000);
    running_snapshot_t paused2 = running_engine_snapshot(&e.running, 5000);
    CHECK(!paused1.running && paused1.speed_x100_kmh == 0, "暂停快照速度归零");
    CHECK(paused1.cadence_spm == 0 && paused1.stride_cm == 0 && paused1.rsc_speed_1_256_mps == 0,
          "暂停快照步频步幅和RSC速度归零");
    CHECK(paused1.total_distance_m == active.total_distance_m && paused1.elapsed_time_s == active.elapsed_time_s,
          "暂停首次快照冻结累计");
    CHECK(paused2.total_distance_m == active.total_distance_m && paused2.elapsed_time_s == active.elapsed_time_s,
          "暂停多次快照持续冻结累计");
    CHECK(!power_engine_set_run_running(&e, false), "重复Stop幂等");

    power_engine_run_reset(&e);
    running_snapshot_t paused_reset = running_engine_snapshot(&e.running, 1000);
    CHECK(!paused_reset.running, "暂停Reset保持暂停态");
    CHECK(paused_reset.total_distance_m == 0 && paused_reset.elapsed_time_s == 0, "暂停Reset清空累计且不重新推进");
    CHECK(!power_engine_is_dirty(&e), "暂停Reset不置dirty");

    CHECK(power_engine_set_run_running(&e, true), "Start恢复运行态");
    running_snapshot_t resumed = running_engine_snapshot(&e.running, 1000);
    CHECK(resumed.running && resumed.speed_x100_kmh > 0 && resumed.cadence_spm > 0, "恢复后重新输出跑步数据");
    CHECK(resumed.total_distance_m > 0 && resumed.elapsed_time_s == 1, "恢复后累计继续推进");
    CHECK(!power_engine_is_dirty(&e), "Start不置dirty");

    power_engine_run_reset(&e);
    running_snapshot_t active_reset = running_engine_snapshot(&e.running, 0);
    CHECK(active_reset.running, "运行Reset保持运行态");
    CHECK(active_reset.total_distance_m == 0 && active_reset.elapsed_time_s == 0, "运行Reset清空累计");

    CHECK(power_engine_set_run_running(&e, false), "profile切换前再次暂停");
    CHECK(power_engine_set_sport_profile(&e, SPORT_PROFILE_CYCLING), "切回骑行profile");
    power_engine_clear_dirty(&e);
    CHECK(power_engine_set_sport_profile(&e, SPORT_PROFILE_RUNNING), "再次切入跑步profile");
    CHECK(e.running.running, "切入跑步profile默认恢复运行态");
}

static void test_session_config_export(void)
{
    printf("[test] 会话态导出持久配置规范化\n");
    power_engine_t e;
    power_engine_init(&e);
    CHECK(power_engine_set_power(&e, 180), "设置持久base功率");
    power_engine_clear_dirty(&e);
    CHECK(power_engine_set_erg_target(&e, 260), "进入ERG会话");
    CHECK(!power_engine_is_dirty(&e), "ERG会话本身保持clean");
    CHECK(power_engine_set_power_float(&e, e.power_float + 1), "ERG期间修改其他持久配置");

    config_data_t exported;
    config_store_from_engine(&e, &exported);
    CHECK(exported.mode == RIDE_MODE_STEADY, "ERG会话导出为steady");
    CHECK(exported.power == 180, "ERG会话导出原base功率而非目标功率");
    CHECK(exported.power_float == e.power_float, "ERG期间其他持久配置正常导出");
    CHECK(e.mode == RIDE_MODE_ERG && e.erg_active_target == 260, "配置导出不改变ERG运行态");

    CHECK(power_engine_set_sport_profile(&e, SPORT_PROFILE_RUNNING), "导出前切到跑步profile");
    CHECK(power_engine_set_run_running(&e, false), "导出前暂停跑步会话");
    config_store_from_engine(&e, &exported);
    CHECK(exported.sport_profile == SPORT_PROFILE_RUNNING, "跑步profile作为配置正常导出");
    CHECK(!e.running.running, "配置导出不改变非持久跑步暂停态");
}

static void test_config_migrate_v9_to_current_running_fields(void)
{
    printf("[test] v9 到当前版本跑步配置迁移\n");
    config_data_t c;
    config_store_defaults(&c);
    c.schema_ver = 9;
    c.power = 333;
    c.run_speed_x100 = 1234;
    c.run_pace_float_sec = 12;
    c.run_cadence = 181;
    c.run_cadence_float = 6;
    CHECK(config_store_migrate(&c), "v9 配置迁移到当前版本");
    CHECK(c.schema_ver == CONFIG_VERSION, "v9 迁移后 schema=当前版本");
    CHECK(c.power == 333, "v9 迁移保留功率");
    CHECK(c.run_speed_x100 == 1234, "v9 迁移保留跑步速度");
    CHECK(c.run_pace_float_sec == 12, "v9 迁移保留跑步配速浮动");
    CHECK(c.run_cadence == 181, "v9 迁移保留跑步步频");
    CHECK(c.run_cadence_float == 6, "v9 迁移保留跑步步频浮动");
    CHECK(c.run_mode == RUN_MODE_FIXED, "v9 迁移补默认跑步模式");
    CHECK(c.run_pacer_type == RUN_PACER_JOG, "v9 迁移补默认跑步模板");
    CHECK(c.run_pace_low_sec == 390 && c.run_pace_high_sec == 450, "v9 迁移补默认模板区间");
    CHECK(strcmp(c.run_custom_name, RUN_CUSTOM_NAME_DEFAULT) == 0, "v9 迁移补默认自定义跑步名称");
}

static void test_config_migrate_v11_to_v12_power_float(void)
{
    printf("[test] v11 到 v12 功率浮动选择性迁移\n");
    CHECK(POWER_FLOAT_LEGACY_DEFAULT == 5, "历史功率浮动默认值锁定为 ±5W");

    config_data_t legacy;
    config_store_defaults(&legacy);
    legacy.schema_ver = 11;
    legacy.power = 260;
    legacy.power_float = POWER_FLOAT_LEGACY_DEFAULT;
    CHECK(config_store_migrate(&legacy), "v11 历史默认配置执行迁移");
    CHECK(legacy.schema_ver == CONFIG_VERSION, "v11 历史默认迁移到 v12");
    CHECK(legacy.power == 260, "v11 迁移保留基础功率");
    CHECK(legacy.power_float == POWER_FLOAT_DEFAULT, "历史默认 ±5W 迁移为 FIT 默认 ±50W");

    config_data_t custom;
    config_store_defaults(&custom);
    custom.schema_ver = 11;
    custom.power = 275;
    custom.power_float = 12;
    CHECK(config_store_migrate(&custom), "v11 用户自定义配置执行迁移");
    CHECK(custom.schema_ver == CONFIG_VERSION, "v11 用户自定义配置迁移到 v12");
    CHECK(custom.power == 275, "v11 用户自定义迁移保留基础功率");
    CHECK(custom.power_float == 12, "用户自定义 ±12W 保持不变");

    CHECK(!config_store_migrate(&custom), "v12 配置重复迁移保持幂等");
    CHECK(custom.power_float == 12, "v12 幂等迁移不改用户浮动");
}

static void test_device_identity_codec(void)
{
    printf("[test] DIS 设备身份覆盖 编解码/校验/回退\n");
    device_identity_t id;
    device_identity_defaults(&id);
    CHECK(!device_identity_has_override(&id), "默认无覆盖字段");
    CHECK(strcmp(device_identity_effective(&id, DEVICE_IDENTITY_MODEL, "ESP32-C3"), "ESP32-C3") == 0,
          "空覆盖回退到固件内置值");

    strcpy(id.fields[DEVICE_IDENTITY_MANUFACTURER], "Magene");
    strcpy(id.fields[DEVICE_IDENTITY_MODEL], "P325-CS");
    strcpy(id.fields[DEVICE_IDENTITY_FIRMWARE], "1.2.8");
    strcpy(id.fields[DEVICE_IDENTITY_SOFTWARE], "迈金");
    CHECK(device_identity_has_override(&id), "写入后存在覆盖字段");

    uint8_t buf[DEVICE_IDENTITY_WIRE_MAX];
    size_t len = device_identity_encode(&id, buf, sizeof(buf));
    CHECK(len == 2 + 7 + 8 + 1 + 1 + 6 + 7, "编码长度=版本+数量+各字段长度前缀与内容");
    CHECK(buf[0] == DEVICE_IDENTITY_PROTOCOL_VERSION && buf[1] == DEVICE_IDENTITY_FIELD_COUNT,
          "编码头部为版本与字段数");
    CHECK(device_identity_encode(&id, buf, len - 1) == 0, "缓冲区不足时编码返回0");

    device_identity_t back;
    device_identity_defaults(&back);
    CHECK(device_identity_decode(buf, len, &back), "合法负载解码成功");
    CHECK(memcmp(&id, &back, sizeof(id)) == 0, "编解码往返一致");
    CHECK(strcmp(device_identity_effective(&back, DEVICE_IDENTITY_MANUFACTURER, "BikePower"), "Magene") == 0,
          "覆盖值优先于内置值");

    device_identity_t untouched = back;
    const uint8_t bad_ver[] = {2, 1, 0};
    CHECK(!device_identity_decode(bad_ver, sizeof(bad_ver), &back), "拒绝错误协议版本");
    const uint8_t zero_count[] = {1, 0};
    CHECK(!device_identity_decode(zero_count, sizeof(zero_count), &back), "拒绝字段数为0");
    const uint8_t too_many[] = {1, 7, 0, 0, 0, 0, 0, 0, 0};
    CHECK(!device_identity_decode(too_many, sizeof(too_many), &back), "拒绝字段数超过总数");
    const uint8_t truncated[] = {1, 1, 5, 'A', 'B'};
    CHECK(!device_identity_decode(truncated, sizeof(truncated), &back), "拒绝长度越界");
    const uint8_t trailing[] = {1, 1, 1, 'A', 'Z'};
    CHECK(!device_identity_decode(trailing, sizeof(trailing), &back), "拒绝尾部多余字节");
    const uint8_t ctrl[] = {1, 1, 2, 'A', '\n'};
    CHECK(!device_identity_decode(ctrl, sizeof(ctrl), &back), "拒绝控制字符");
    const uint8_t bad_utf8[] = {1, 1, 2, 0xE8, 0xBF};
    CHECK(!device_identity_decode(bad_utf8, sizeof(bad_utf8), &back), "拒绝截断的 UTF-8");
    uint8_t long_field[2 + 1 + 32];
    long_field[0] = 1; long_field[1] = 1; long_field[2] = 32;
    memset(long_field + 3, 'X', 32);
    CHECK(!device_identity_decode(long_field, sizeof(long_field), &back), "拒绝超过31字节字段");
    CHECK(memcmp(&untouched, &back, sizeof(back)) == 0, "解码失败时输出保持不变");

    const uint8_t partial[] = {1, 2, 3, 'A', 'B', 'C', 2, 'M', '1'};
    CHECK(device_identity_decode(partial, sizeof(partial), &back), "允许仅携带前若干字段");
    CHECK(strcmp(back.fields[DEVICE_IDENTITY_MODEL], "M1") == 0 && back.fields[DEVICE_IDENTITY_FIRMWARE][0] == 0,
          "未携带字段清空为回退值");

    const uint8_t reset[] = {1, 6, 0, 0, 0, 0, 0, 0};
    CHECK(device_identity_decode(reset, sizeof(reset), &back) && !device_identity_has_override(&back),
          "全空负载恢复全部内置值");
}

static void test_ble_config_codec(void)
{
    printf("[test] BLE 配置协议全字段往返与边界\n");
    config_data_t source;
    config_store_defaults(&source);
    source.power = POWER_MAX;
    source.cadence = CADENCE_MAX;
    source.mode = RIDE_MODE_ROAD;
    source.heartrate = HR_MAX;
    source.has_user_hr = 1;
    source.power_float = POWER_FLOAT_MAX;
    source.cadence_float = CADENCE_FLOAT_MAX;
    source.hr_float = HR_FLOAT_MAX;
    source.power_step = POWER_STEP_MAX;
    source.weight = PROFILE_WEIGHT_MAX;
    source.ftp = PROFILE_FTP_MAX;
    source.thr_hr = PROFILE_THR_HR_MAX;
    source.ride_type = PACER_INDEX_CUSTOM;
    source.custom_wkg_low = 61;
    source.custom_wkg_high = CUSTOM_WKG_MAX;
    strcpy(source.custom_pacer_name, "山地-Tempo");
    source.sport_profile = SPORT_PROFILE_RUNNING;
    source.broadcast_mask = BROADCAST_FIELD_POWER | BROADCAST_FIELD_HEARTRATE;
    source.run_speed_x100 = RUN_SPEED_X100_MAX;
    source.run_pace_float_sec = RUN_PACE_FLOAT_SEC_MAX;
    source.run_cadence = RUN_CADENCE_MAX;
    source.run_cadence_float = RUN_CADENCE_FLOAT_MAX;
    source.run_mode = RUN_MODE_PACER;
    source.run_pacer_type = RUN_PACER_CUSTOM;
    source.run_pace_low_sec = RUN_PACE_SEC_MIN;
    source.run_pace_high_sec = RUN_PACE_SEC_MAX;
    strcpy(source.run_custom_name, "夜跑-自定义");
    strcpy(source.device_name, "BikePower-C3");
    source.led.silent = 1;
    for (int i = 0; i < LED_SCENE_COUNT; i++) {
        source.led.scenes[i].mode = (uint8_t)(i % LED_MODE_COUNT);
        source.led.scenes[i].color = (uint8_t)(i % LED_COLOR_COUNT);
    }

    uint8_t meta[BLE_CONFIG_META_LEN];
    CHECK(ble_config_encode_meta(meta, sizeof(meta), 0x78563412u) == BLE_CONFIG_META_LEN,
          "Meta 固定长度编码");
    CHECK(meta[0] == BLE_CONFIG_PROTOCOL_VERSION && meta[1] == CONFIG_VERSION,
          "Meta 携带协议与 schema 版本");
    CHECK(meta[2] == BLE_CONFIG_GROUP_MASK_ALL && meta[3] == BLE_CONFIG_CAPABILITIES,
          "Meta 声明全组读写事务能力");
    CHECK(meta[4] == 0x12 && meta[5] == 0x34 && meta[6] == 0x56 && meta[7] == 0x78,
          "Meta revision 使用小端");

    config_data_t decoded;
    config_store_defaults(&decoded);
    uint8_t payload[BLE_CONFIG_GROUP_MAX_LEN];
    for (int group = 0; group < BLE_CONFIG_GROUP_COUNT; group++) {
        memset(payload, 0, sizeof(payload));
        size_t len = ble_config_encode_group((ble_config_group_t)group, &source,
                                             payload, sizeof(payload));
        CHECK(len > 0 && len <= BLE_CONFIG_GROUP_MAX_LEN, "配置组长度适配 MTU 64");
        CHECK(ble_config_decode_group((ble_config_group_t)group, payload, len, &decoded),
              "配置组可解码");
    }

    char source_json[2048];
    char decoded_json[2048];
    size_t source_len = config_store_serialize(&source, source_json, sizeof(source_json));
    size_t decoded_len = config_store_serialize(&decoded, decoded_json, sizeof(decoded_json));
    CHECK(source_len > 0 && decoded_len == source_len, "全配置往返序列化长度一致");
    CHECK(strcmp(source_json, decoded_json) == 0, "全部 NVS 配置字段二进制往返一致");

    config_data_t before = decoded;
    size_t core_len = ble_config_encode_group(BLE_CONFIG_GROUP_CORE, &source,
                                              payload, sizeof(payload));
    payload[0] = BLE_CONFIG_PROTOCOL_VERSION + 1;
    CHECK(!ble_config_decode_group(BLE_CONFIG_GROUP_CORE, payload, core_len, &decoded),
          "拒绝未知协议版本");
    CHECK(memcmp(&before, &decoded, sizeof(decoded)) == 0, "解码失败不污染正式配置");
    payload[0] = BLE_CONFIG_PROTOCOL_VERSION;
    CHECK(!ble_config_decode_group(BLE_CONFIG_GROUP_CORE, payload, core_len - 1, &decoded),
          "拒绝截断配置组");

    uint8_t response[BLE_CONFIG_CONTROL_RESPONSE_LEN];
    CHECK(ble_config_encode_control_response(response, sizeof(response), BLE_CONFIG_OP_COMMIT,
                                              BLE_CONFIG_RESULT_SUCCESS,
                                              BLE_CONFIG_RESTART_GATT | BLE_CONFIG_RESTART_NAME,
                                              0x04030201u, 0x08070605u)
              == BLE_CONFIG_CONTROL_RESPONSE_LEN,
          "控制点响应固定长度编码");
    CHECK(response[0] == BLE_CONFIG_OP_RESPONSE && response[1] == BLE_CONFIG_OP_COMMIT
              && response[2] == BLE_CONFIG_RESULT_SUCCESS && response[3] == 3,
          "控制点响应头字段正确");
    CHECK(response[4] == 1 && response[7] == 4 && response[8] == 5 && response[11] == 8,
          "控制点 transaction/revision 使用小端");
}

static void test_config_apply_to_engine(void)
{
    printf("[test] 完整配置原子应用与会话保持\n");
    power_engine_t engine;
    power_engine_init(&engine);
    power_engine_set_power(&engine, 222);
    power_engine_clear_dirty(&engine);
    CHECK(power_engine_set_erg_target(&engine, 310), "应用测试前进入 ERG 会话");

    config_data_t config;
    config_store_from_engine(&engine, &config);
    config.cadence = 101;
    config.has_user_hr = 0;
    config.power_float = 17;
    strcpy(config.device_name, "BikePower-Edit");
    CHECK(config_store_apply_to_engine(&config, &engine), "合法完整配置应用成功");
    CHECK(engine.base_power == 222 && engine.cadence_base == 101 && engine.power_float == 17,
          "基础配置完整应用");
    CHECK(!engine.has_user_hr, "关闭固定心率会清除运行时锁定");
    CHECK(engine.mode == RIDE_MODE_ERG && engine.has_erg_target && engine.erg_active_target == 310,
          "未改变持久模式时保留 ERG 会话");
    CHECK(strcmp(engine.device_name, "BikePower-Edit") == 0, "设备名同步应用");

    power_engine_t unchanged = engine;
    config.power = POWER_MAX + 1;
    CHECK(!config_store_apply_to_engine(&config, &engine), "越界完整配置被拒绝");
    CHECK(memcmp(&unchanged, &engine, sizeof(engine)) == 0, "应用失败不污染运行时引擎");
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
    test_broadcast_fields_and_button_target();
    test_button_operation_scenario();
    test_random_power_hr_following_scenario();
    test_linked_hr_follows_pacer_and_running_speed();
    test_float_effect_scenario();
    test_reference_ride_simulation();
    test_reference_ride_multiseed();
    test_ride_model_edge_cases();
    test_heartrate_lock_unlock_extremes();
    test_ride_model_24h_equivalent();
    test_irregular_notification_sampling();
    test_web_config_persistence_scenario();
    test_engine_blindspots();
    test_config_store();
    test_json_miniparse();
    test_device_templates();
    test_device_analysis();
    test_button_logic();
    test_led_pattern();
    test_button_debounce();
    test_running_engine_defaults();
    test_running_pacer_defaults();
    test_running_pacer_templates();
    test_running_custom_pacer_validation();
    test_running_control_values();
    test_running_session_state();
    test_session_config_export();
    test_config_migrate_v9_to_current_running_fields();
    test_config_migrate_v11_to_v12_power_float();
    test_ble_config_codec();
    test_device_identity_codec();
    test_config_apply_to_engine();
    printf("\n结果: %d 通过, %d 失败\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
