#pragma once

#include <stdint.h>
#include "device_telemetry.h"

/* 主任务调用，每秒采样；与配置 revision 和 NVS 完全独立。 */
void device_telemetry_sample(uint8_t state, uint8_t profile);
/* 编码真实构建元数据，固定 20 字节。 */
void device_telemetry_meta(uint8_t out[TELEMETRY_RECORD_LEN]);
