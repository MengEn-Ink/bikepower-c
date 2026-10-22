# 领域层 host 单元测试
# 用法: make && ./build/test_domain
DOMAIN := ../components/bikepower_domain
CC ?= clang
CFLAGS := -std=c11 -Wall -Wextra -I$(DOMAIN)/include
DOMAIN_SRCS := $(DOMAIN)/profile.c \
        $(DOMAIN)/domain_rng.c \
        $(DOMAIN)/hr_model.c \
        $(DOMAIN)/power_engine.c \
        $(DOMAIN)/running_engine.c \
        $(DOMAIN)/config_store.c \
        $(DOMAIN)/device_template.c \
        $(DOMAIN)/device_analysis.c \
        $(DOMAIN)/json_miniparse.c \
        $(DOMAIN)/button_logic.c \
        $(DOMAIN)/led_pattern.c \
        $(DOMAIN)/button_debounce.c
CODEC_SRC := $(DOMAIN)/ble_payload_codec.c
TEST_SRCS := test_domain.c $(DOMAIN_SRCS) $(CODEC_SRC)

BUILD_DIR := build

.PHONY: all run clean
all: $(BUILD_DIR) $(BUILD_DIR)/test_domain $(BUILD_DIR)/ble_codec_cli

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/test_domain: $(TEST_SRCS)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS) -lm

$(BUILD_DIR)/ble_codec_cli: ble_codec_cli.c $(CODEC_SRC)
	$(CC) $(CFLAGS) -o $@ ble_codec_cli.c $(CODEC_SRC)

run: all
	./$(BUILD_DIR)/test_domain

clean:
	rm -rf $(BUILD_DIR)
