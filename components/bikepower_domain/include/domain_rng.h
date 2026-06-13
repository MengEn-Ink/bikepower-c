/**
 * 领域层随机数（可注入种子，便于 host 单测确定性复现）
 * 对应 MicroPython random.randint(a, b)（闭区间）。
 */
#pragma once

#include <stdint.h>

/* 设置伪随机数发生器种子，便于测试时复现结果。 */
void domain_rng_seed(uint32_t seed);

/* 返回 [lo, hi] 闭区间内的随机整数。 */
int domain_rng_range(int lo, int hi);
