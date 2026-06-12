/**
 * 领域层随机数实现 — xorshift32，闭区间映射。
 */
#include "domain_rng.h"

static uint32_t s_state = 0x12345678u;

void domain_rng_seed(uint32_t seed)
{
    s_state = seed ? seed : 0x12345678u;
}

static uint32_t xorshift32(void)
{
    uint32_t x = s_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_state = x;
    return x;
}

int domain_rng_range(int lo, int hi)
{
    if (hi <= lo) return lo;
    uint32_t span = (uint32_t)(hi - lo) + 1u;
    return lo + (int)(xorshift32() % span);
}
