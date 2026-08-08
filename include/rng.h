#ifndef RNG_H
#define RNG_H

#include <stdint.h>

typedef struct {
    uint64_t state;
    uint64_t inc;
} Rng;

void rng_seed(Rng *rng, uint64_t seed, uint64_t seq);
uint32_t rng_next_u32(Rng *rng);
int rng_below(Rng *rng, int bound);
double rng_double(Rng *rng);

#endif
