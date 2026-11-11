# 领域层 host 单元测试
# 用法: make && ./test_domain
DOMAIN := ../components/bikepower_domain
CC ?= clang
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
CFLAGS := -std=c11 -Wall -Wextra -I$(DOMAIN)/include
DOMAIN_SRCS := $(DOMAIN)/profile.c \
        $(DOMAIN)/domain_rng.c \
        $(DOMAIN)/hr_model.c \
        $(DOMAIN)/power_engine.c \
        $(DOMAIN)/running_engine.c \
        $(DOMAIN)/config_store.c \
        $(DOMAIN)/device_identity.c \
        $(DOMAIN)/device_analysis.c \
        $(DOMAIN)/json_miniparse.c \
        $(DOMAIN)/button_logic.c \
        $(DOMAIN)/led_pattern.c \
        $(DOMAIN)/button_debounce.c
DOMAIN_SRCS += $(DOMAIN)/cps_input_codec.c $(DOMAIN)/power_bridge.c
DOMAIN_SRCS += $(DOMAIN)/bridge_failover.c
CODEC_SRC := $(DOMAIN)/ble_payload_codec.c
CONFIG_CODEC_SRC := $(DOMAIN)/ble_config_codec.c
TEST_SRCS := test_domain.c $(DOMAIN_SRCS) $(CODEC_SRC) $(CONFIG_CODEC_SRC)
HEADERS := $(wildcard $(DOMAIN)/include/*.h)

.PHONY: all run clean
all: test_domain ble_codec_cli test_power_bridge test_bridge_failover test_device_telemetry

test_domain: $(TEST_SRCS) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(TEST_SRCS) -lm

ble_codec_cli: ble_codec_cli.c $(CODEC_SRC) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ ble_codec_cli.c $(CODEC_SRC)

test_power_bridge: test_power_bridge.c $(DOMAIN_SRCS) $(CONFIG_CODEC_SRC) $(CODEC_SRC) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(filter %.c,$^) -lm

test_bridge_failover: test_bridge_failover.c $(DOMAIN)/bridge_failover.c \
		$(DOMAIN)/power_bridge.c $(DOMAIN)/device_identity.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(filter %.c,$^)

test_device_telemetry: test_device_telemetry.c $(DOMAIN)/device_telemetry.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(filter %.c,$^)

run: all
	./test_domain
	./test_power_bridge
	./test_bridge_failover
	./test_device_telemetry

clean:
	rm -f test_domain test_domain.exe ble_codec_cli ble_codec_cli.exe \
		test_power_bridge test_bridge_failover test_device_telemetry
