#pragma once

#include "config_snapshot.h"

typedef enum {
    PERSISTENT_STORE_OK,
    PERSISTENT_STORE_ABSENT,
    PERSISTENT_STORE_MIGRATION_INCOMPLETE,
    PERSISTENT_STORE_INVALID,
    PERSISTENT_STORE_IO_ERROR,
} persistent_store_result_t;

persistent_store_result_t persistent_store_load(config_data_t *config, device_identity_t *identity);
void persistent_store_seed(const config_data_t *config, const device_identity_t *identity);
void persistent_store_readonly_defaults(void);
void persistent_store_readonly(const config_data_t *config, const device_identity_t *identity);
bool persistent_store_save(const config_data_t *config, const device_identity_t *identity);
bool persistent_store_save_identity(const device_identity_t *identity);
bool persistent_store_identity(device_identity_t *identity);
