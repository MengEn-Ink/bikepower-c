/**
 * 领域层常量（纯 C，不依赖 ESP-IDF，可 host 编译）。
 */
#pragma once

// ===== 功率/踏频/心率范围 =====
#define POWER_MIN             0
#define POWER_MAX             2000
#define CADENCE_MIN           20
#define CADENCE_MAX           120
#define HR_MIN                60
#define HR_MAX                200

// ===== 调功步进（可配网修改并持久化） =====
#define POWER_STEP_W          20      // 物理按钮短按/长按调功步进默认值（历史常量，作为默认）
#define POWER_STEP_MIN        5       // 可配置步进下限
#define POWER_STEP_MAX        50      // 可配置步进上限
#define POWER_STEP_DEFAULT    20      // 可配置步进默认值

// ===== 默认值 =====
#define DEFAULT_POWER         260
#define DEFAULT_CADENCE       90
#define DEFAULT_HEARTRATE     150

// ===== 跑步模式范围与默认值 =====
#define RUN_SPEED_X100_MIN     300     // 3.00 km/h
#define RUN_SPEED_X100_MAX     2500    // 25.00 km/h
#define RUN_SPEED_X100_DEFAULT 1000    // 10.00 km/h
#define RUN_PACE_FLOAT_SEC_MIN 0       // ±秒/km
#define RUN_PACE_FLOAT_SEC_MAX 60
#define RUN_PACE_FLOAT_SEC_DEFAULT 10
#define RUN_CADENCE_MIN        120     // steps/min
#define RUN_CADENCE_MAX        220
#define RUN_CADENCE_DEFAULT    170
#define RUN_CADENCE_FLOAT_MIN  0       // ±steps/min
#define RUN_CADENCE_FLOAT_MAX  20
#define RUN_CADENCE_FLOAT_DEFAULT 4
#define RUN_PACE_SEC_MIN       144     // 2:24 /km，25.0 km/h
#define RUN_PACE_SEC_MAX       1200    // 20:00 /km，3.0 km/h
#define RUN_CUSTOM_NAME_MAX    32      // 自定义跑步档名称缓冲区（含结尾 \0）
#define RUN_CUSTOM_NAME_DEFAULT "自定义"

// ===== 配置格式版本 =====
// v11：移除未落地的跑步坡度持久化字段，仅保留速度/步频/配速核心参数。
#define CONFIG_VERSION        11

// ===== 设备名称（BLE 广播名，可配网修改并持久化） =====
#define DEVICE_NAME_MAX       24
#define DEVICE_NAME_DEFAULT   "BikePower"

// ===== 运动 Profile =====
typedef enum {
    SPORT_PROFILE_CYCLING = 0,
    SPORT_PROFILE_RUNNING = 1,
} sport_profile_t;

#define SPORT_PROFILE_COUNT       2
#define DEFAULT_SPORT_PROFILE     SPORT_PROFILE_CYCLING

// ===== BLE 数据项启用开关 =====
typedef enum {
    BROADCAST_FIELD_POWER     = 1 << 0,
    BROADCAST_FIELD_CADENCE   = 1 << 1,
    BROADCAST_FIELD_HEARTRATE = 1 << 2,
} broadcast_field_t;

#define BROADCAST_FIELD_ALL (BROADCAST_FIELD_POWER | BROADCAST_FIELD_CADENCE | BROADCAST_FIELD_HEARTRATE)

// ===== 跑步输出模式 =====
typedef enum {
    RUN_MODE_FIXED = 0,
    RUN_MODE_PACER = 1,
} run_mode_t;

#define RUN_MODE_COUNT 2
#define RUN_MODE_DEFAULT RUN_MODE_FIXED

typedef enum {
    RUN_PACER_JOG = 0,
    RUN_PACER_AEROBIC,
    RUN_PACER_TEMPO,
    RUN_PACER_INTERVAL,
    RUN_PACER_CUSTOM,
} run_pacer_type_t;

#define RUN_PACER_PRESET_COUNT 4
#define RUN_PACER_COUNT 5
#define RUN_PACER_DEFAULT RUN_PACER_JOG

typedef struct {
    const char *name;
    int pace_low_sec;
    int pace_high_sec;
    int cadence_spm;
} run_pacer_t;

extern const run_pacer_t RUN_PACERS[RUN_PACER_PRESET_COUNT];

// ===== 骑行模式 ID =====
/* 骑行模式枚举：决定功率、踏频和心率快照的生成策略。 */
typedef enum {
    RIDE_MODE_STEADY = 0,
    RIDE_MODE_ROAD   = 1,
    RIDE_MODE_ERG    = 2,
} ride_mode_t;

#define RIDE_MODE_COUNT       3
#define DEFAULT_RIDE_MODE     RIDE_MODE_STEADY

// ===== ERG / road 模式实时抖动（训练 App 驱动，非用户可配） =====
#define STEADY_POWER_FLUCT_LO    (-5)
#define STEADY_POWER_FLUCT_HI    5
#define STEADY_CADENCE_FLUCT_LO  (-2)
#define STEADY_CADENCE_FLUCT_HI  2
#define STEADY_USER_HR_TAU       8
#define STEADY_USER_HR_TARGET_HOLD_LOOPS  100

// ===== 固定模式浮动幅度（±，可配网调整并持久化） =====
// 浮动值为固定基准值的上下限半幅：实际输出在 [base-amp, base+amp] 内平滑游走。
// amp=0 时输出恒定为基准值。
#define POWER_FLOAT_MIN          0
#define POWER_FLOAT_MAX          100
#define POWER_FLOAT_DEFAULT      5
#define CADENCE_FLOAT_MIN        0
#define CADENCE_FLOAT_MAX        20
#define CADENCE_FLOAT_DEFAULT    2
#define HR_FLOAT_MIN             0
#define HR_FLOAT_MAX             20
#define HR_FLOAT_DEFAULT         2

// ===== 平滑随机游走参数（有界游走，非逐拍随机） =====
// 每 FLUCT_WALK_HOLD_LOOPS 拍在浮动带内重采样一个目标，再用一阶指数逼近，输出恒在带内。
#define FLUCT_WALK_TAU           3      // 逼近时间常数（秒）
#define FLUCT_WALK_HOLD_LOOPS    60     // 目标保持循环数（60*50ms=3s）后重采样

// ===== 心率动力学（一阶指数逼近） =====
#define HR_TAU_RISE           30
#define HR_TAU_FALL           60
#define HR_LOOP_INTERVAL_MS   50

// ===== 对外心率台阶保持 =====
#define HR_EMIT_DEADBAND      2
#define HR_EMIT_JUMP_BPS      200

// ===== FTP 心率区间映射（恢复区线性段） =====
#define HR_ZONE_RECOVERY_BASE   42
#define HR_ZONE_RECOVERY_SLOPE  26

// ===== 心率变异性 HRV =====
#define HRV_RSA_AMPLITUDE       2.0f
#define HRV_RSA_FREQ_HZ         0.12f
#define HRV_MAYER_AMPLITUDE     1.0f
#define HRV_MAYER_PERIOD_S      10.0f
#define HRV_DRIFT_RATE_BPM_PER_MIN  0.05f
#define HRV_DRIFT_CAP_BPM       5.0f

// ===== 个人信息范围 =====
#define PROFILE_WEIGHT_MIN      40
#define PROFILE_WEIGHT_MAX      150
#define PROFILE_WEIGHT_DEFAULT  70
#define PROFILE_FTP_MIN         50
#define PROFILE_FTP_MAX         500
#define PROFILE_FTP_DEFAULT     300
#define PROFILE_THR_HR_MIN      100
#define PROFILE_THR_HR_MAX      220
#define PROFILE_THR_HR_DEFAULT  190

// ===== 配速机器人 =====
// 预设索引 0-4 对应 coco/yumi/jacques/genie/constance；索引 5 为用户自定义档（W/kg 范围存于 profile）。
#define PACER_COUNT             5       // 预设配速机器人数量（ROAD_PACERS 数组大小）
#define PACER_INDEX_CUSTOM      5       // 自定义档索引
#define RIDE_TYPE_COUNT         6       // 可选档位总数（5 预设 + 1 自定义）
#define PACER_INDEX_DEFAULT     0
#define PROFILE_RIDE_TYPE_DEFAULT  PACER_INDEX_DEFAULT
#define PACER_INDEX_IS_VALID(idx)  ((idx) >= 0 && (idx) < RIDE_TYPE_COUNT)
#define PACER_INDEX_IS_CUSTOM(idx) ((idx) == PACER_INDEX_CUSTOM)

// 自定义档 W/kg 范围（存储为 W/kg * 10 的整数）
#define CUSTOM_WKG_MIN          10      // 1.0 W/kg
#define CUSTOM_WKG_MAX          70      // 7.0 W/kg
#define CUSTOM_WKG_LOW_DEFAULT  25      // 2.5 W/kg
#define CUSTOM_WKG_HIGH_DEFAULT 30      // 3.0 W/kg
#define CUSTOM_PACER_NAME_MAX   32      // 自定义配速机器人昵称缓冲区（含结尾 \0）
#define CUSTOM_PACER_NAME_DEFAULT "自定义"

typedef struct {
    const char *name;
    int wkg_low;   // W/kg * 10
    int wkg_high;  // W/kg * 10
} road_pacer_t;

extern const road_pacer_t ROAD_PACERS[PACER_COUNT];
