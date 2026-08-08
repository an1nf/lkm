#include "rng.h"

void rng_seed(Rng *rng, uint64_t seed, uint64_t seq) {
    rng->state = 0U;
    rng->inc = (seq << 1u) | 1u;
    rng_next_u32(rng);
    rng->state += seed;
    rng_next_u32(rng);
}

uint32_t rng_next_u32(Rng *rng) {
    uint64_t oldstate = rng->state;
    rng->state = oldstate * 6364136223846793005ULL + rng->inc;
    uint32_t xorshifted = (uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = (uint32_t)(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

int rng_below(Rng *rng, int bound) {
    uint32_t threshold = (uint32_t)(-bound) % (uint32_t)bound;
    for (;;) {
        uint32_t r = rng_next_u32(rng);
        if (r >= threshold) return (int)(r % (uint32_t)bound);
    }
}

double rng_double(Rng *rng) {
    return (double)(rng_next_u32(rng) >> 8) / (double)(1u << 24);
}
