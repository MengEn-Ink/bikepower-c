/**
 * 领域层随机数实现 — xorshift32，闭区间映射。
 */
#include "domain_rng.h"

static uint32_t s_state = 0x12345678u;

/*
 * 设置随机数发生器种子。
 * 参数：seed 为新的随机种子。
 * 返回值：无。
 * 特殊行为：当传入 0 时会回退到固定默认种子，避免内部状态落到无效值。
 */
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

/*
 * 生成闭区间随机整数。
 * 参数：lo 为下界；hi 为上界。
 * 返回值：返回 [lo, hi] 范围内的随机整数。
 * 特殊行为：当 hi 小于等于 lo 时直接返回 lo，避免出现无效区间计算。
 */
int domain_rng_range(int lo, int hi)
{
    if (hi <= lo) return lo;
    uint32_t span = (uint32_t)(hi - lo) + 1u;
    return lo + (int)(xorshift32() % span);
}
