/**
 * DIS 设备身份覆盖 NVS 持久化。
 * 只能在 app_main 任务上下文调用（会阻塞写 flash），禁止在 NimBLE host 任务中调用。
 */
#pragma once

#include <stdbool.h>
#include "device_identity.h"

/* 读取已保存的身份覆盖；不存在或损坏时输出全空（全部回退内置值）。 */
void device_identity_store_load(device_identity_t *id);

/* 保存身份覆盖；全部字段为空时删除键以恢复出厂。返回是否成功提交。 */
bool device_identity_store_save(const device_identity_t *id);
