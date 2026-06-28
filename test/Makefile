# 领域层 host 单元测试
# 用法: make && ./test_domain
DOMAIN := ../components/bikepower_domain
CC ?= clang
CFLAGS := -std=c11 -Wall -Wextra -I$(DOMAIN)/include
SRCS := test_domain.c \
        $(DOMAIN)/profile.c \
        $(DOMAIN)/domain_rng.c \
        $(DOMAIN)/hr_model.c \
        $(DOMAIN)/power_engine.c \
        $(DOMAIN)/config_store.c \
        $(DOMAIN)/ota_version.c \
        $(DOMAIN)/json_miniparse.c

test_domain: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) -lm

.PHONY: run clean
run: test_domain
	./test_domain

clean:
	rm -f test_domain
