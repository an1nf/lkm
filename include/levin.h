#ifndef LEVIN_H
#define LEVIN_H

#include <stdbool.h>
#include "vm.h"
#include "rng.h"

#define ENUM_LIMIT 70000
#define LEVIN_MAX_ENUM_LEN 5

typedef struct {
    unsigned char program[MAX_PROG_LEN];
    int prog_len;
    bool found;
    int result_len;
    int output[MAX_OUT];
    int output_len;
    long steps;
    long programs;
    long phases;
    double wall_time;
    bool stochastic;
} LResult;

typedef struct {
    int K_upper;
    double ratio;
    unsigned char program[MAX_PROG_LEN];
    int prog_len;
    bool has_program;
    long steps;
} KResult;

long ipow(long base, int exp);
bool next_tuple(unsigned char *tup, int L, int base);
bool output_matches_target(const VMResult *out, const int *target, int target_len);

LResult levin_search(const int *target, int target_len, int max_len,
                      long total_budget, bool verbose, Rng *rng);

KResult kolmogorov_upper(const int *data, int data_len, int max_len,
                          long total_budget, Rng *rng);

#endif
