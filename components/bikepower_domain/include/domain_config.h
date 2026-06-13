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
#define CONFIG_VERSION        4

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

// ===== 模式范围调节 =====
#define MODE_TUNE_MIN         0
#define MODE_TUNE_MAX         100
#define MODE_TUNE_DEFAULT     50

// ===== 固定功率模式波动 =====
#define STEADY_POWER_FLUCT_LO    (-5)
#define STEADY_POWER_FLUCT_HI    5
#define STEADY_CADENCE_FLUCT_LO  (-2)
#define STEADY_CADENCE_FLUCT_HI  2
#define STEADY_USER_HR_FLUCT_LO  (-2)
#define STEADY_USER_HR_FLUCT_HI  2
#define STEADY_USER_HR_TAU       8
#define STEADY_USER_HR_TARGET_HOLD_LOOPS  100

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
#define PROFILE_RIDE_TYPE_DEFAULT  0
