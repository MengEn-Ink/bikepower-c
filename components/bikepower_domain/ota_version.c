/**
 * OTA 语义版本比较（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 从 main/ota_update.c 抽取，便于 host 单元测试覆盖防降级逻辑。
 */
#include "ota_version.h"
#include <string.h>
#include <stdio.h>

int ota_version_compare(const char *remote, const char *current)
{
    int r_major = 0, r_minor = 0, r_patch = 0;
    int c_major = 0, c_minor = 0, c_patch = 0;
    if (sscanf(remote, "%d.%d.%d", &r_major, &r_minor, &r_patch) != 3) {
        return strcmp(remote, current);
    }
    if (sscanf(current, "%d.%d.%d", &c_major, &c_minor, &c_patch) != 3) {
        return strcmp(remote, current);
    }
    if (r_major != c_major) return r_major > c_major ? 1 : -1;
    if (r_minor != c_minor) return r_minor > c_minor ? 1 : -1;
    if (r_patch != c_patch) return r_patch > c_patch ? 1 : -1;
    return 0;
}
