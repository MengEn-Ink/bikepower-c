#pragma once

#include "config_store.h"
#include "device_identity.h"

#define CONFIG_SNAPSHOT_VERSION 1u
#define CONFIG_SNAPSHOT_MAGIC 0x42505331u
#define CONFIG_SNAPSHOT_HEADER_LEN 16u
#define CONFIG_SNAPSHOT_MAX_LEN 640u
#define CONFIG_SNAPSHOT_MARKER_LEN 16u

uint32_t config_snapshot_crc(const uint8_t *data, size_t length);
size_t config_snapshot_encode(const config_data_t *config, const device_identity_t *identity,
                              uint32_t generation, uint8_t *out, size_t capacity);
bool config_snapshot_decode(const uint8_t *data, size_t length, config_data_t *config,
                            device_identity_t *identity, uint32_t *generation);
void config_snapshot_marker_encode(unsigned slot, uint32_t generation,
                                   uint8_t out[CONFIG_SNAPSHOT_MARKER_LEN]);
bool config_snapshot_marker_decode(const uint8_t *data, size_t length,
                                   unsigned *slot, uint32_t *generation);
