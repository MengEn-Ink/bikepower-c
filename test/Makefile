# 领域层 host 单元测试
# 用法: make && ./test_domain
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

.PHONY: all run clean
all: test_domain ble_codec_cli

test_domain: $(TEST_SRCS)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS) -lm

ble_codec_cli: ble_codec_cli.c $(CODEC_SRC)
	$(CC) $(CFLAGS) -o $@ ble_codec_cli.c $(CODEC_SRC)

run: all
	./test_domain

clean:
	rm -f test_domain test_domain.exe ble_codec_cli ble_codec_cli.exe
