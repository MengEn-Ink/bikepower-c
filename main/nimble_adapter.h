#pragma once
#include <stdbool.h>
#include <stdint.h>

#define NIMBLE_ADAPTER_CCCD_INDICATE_FLAG 0x02

/* 仅在 NimBLE host 上下文调用；不返回协议栈私有指针。 */
bool nimble_adapter_indication_idle(uint16_t connection);
