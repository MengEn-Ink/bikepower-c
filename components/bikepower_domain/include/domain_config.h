/**
 * 领域层常量（纯 C，不依赖 ESP-IDF，可 host 编译）
 * 由 MicroPython config.py 移植，保持功率/心率/踏频算法位级一致。
 */
#pragma once

/* 功率、踏频、心率、模式和用户档案等领域常量定义集中在本头文件。 */

// ===== 功率/踏频/心率范围 =====
#define POWER_MIN             0
#define POWER_MAX             2000
#define CADENCE_MIN           20
#define CADENCE_MAX           120
#define HR_MIN                60
#define HR_MAX                200

// ===== 调功步进 =====
#define POWER_STEP_W          20

// ===== 默认值 =====
#define DEFAULT_POWER         260
#define DEFAULT_CADENCE       90
#define DEFAULT_HEARTRATE     150

// ===== 配置格式版本 =====
// v5：移除 road 模式无效的 tune_road 字段，新增固定心率持久化（heartrate + has_user_hr）。
#define CONFIG_VERSION        5

// ===== 设备名称（BLE 广播名，可配网修改并持久化） =====
#define DEVICE_NAME_MAX       24
#define DEVICE_NAME_DEFAULT   "BikePower"

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
// 索引 0-4 对应 coco/yumi/jacques/genie/constance
#define PACER_COUNT             5
#define PACER_INDEX_DEFAULT     0
#define PROFILE_RIDE_TYPE_DEFAULT  PACER_INDEX_DEFAULT
#define PACER_INDEX_IS_VALID(idx)  ((idx) >= 0 && (idx) < PACER_COUNT)

typedef struct {
    const char *name;
    int wkg_low;   // W/kg * 10
    int wkg_high;  // W/kg * 10
} road_pacer_t;

extern const road_pacer_t ROAD_PACERS[PACER_COUNT];
