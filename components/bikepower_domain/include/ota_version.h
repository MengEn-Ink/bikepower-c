/**
 * OTA 语义版本比较（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 从 main/ota_update.c 抽取，便于 host 单元测试覆盖防降级逻辑。
 */
#pragma once

/**
 * 语义版本比较（major.minor.patch）。
 * remote：远端版本字符串。
 * current：当前版本字符串。
 * 返回值：>0 表示 remote 更新，==0 相同，<0 表示 remote 更旧（降级）。
 * 特殊行为：非 x.y.z 格式回退到 strcmp。
 */
int ota_version_compare(const char *remote, const char *current);
