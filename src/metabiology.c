#include "metabiology.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int rand_len(Rng *rng, int lo, int hi) {
    return lo + rng_below(rng, hi - lo + 1);
}

int mutate_prog(const unsigned char *prog, int len, double rate,
                 unsigned char *result, Rng *rng) {
    int out_len = 0;
    for (int i = 0; i < len; i++) {
        double r = rng_double(rng);
        unsigned char b = prog[i];
        if (r < rate / 3.0) {
            if (out_len < MAX_PROG_LEN) result[out_len++] = (unsigned char)rng_below(rng, N_OPS);
        } else if (r < 2.0 * rate / 3.0) {
            if (out_len < MAX_PROG_LEN) result[out_len++] = (unsigned char)rng_below(rng, N_OPS);
            if (out_len < MAX_PROG_LEN) result[out_len++] = b;
        } else if (r < rate) {
            /* deletion: skip byte */
        } else {
            if (out_len < MAX_PROG_LEN) result[out_len++] = b;
        }
    }
    if (out_len == 0) {
        result[out_len++] = (unsigned char)rng_below(rng, N_OPS);
    }
    if (out_len > 64) out_len = 64;
    return out_len;
}

int crossover_prog(const unsigned char *a, int alen, const unsigned char *b, int blen,
                    unsigned char *result, Rng *rng) {
    if (alen < 2 || blen < 2) {
        memcpy(result, a, alen);
        return alen;
    }
    int cut_a = 1 + rng_below(rng, alen - 1);
    int cut_b = rng_below(rng, blen - 1);
    int out_len = 0;
    for (int i = 0; i < cut_a && out_len < MAX_PROG_LEN; i++) result[out_len++] = a[i];
    for (int i = cut_b; i < blen && out_len < MAX_PROG_LEN; i++) result[out_len++] = b[i];
    return out_len;
}

double fitness_fn(const VMResult *out, const int *target, int target_len, int plen) {
    if (!out->ok) return -1.0;
    int matches = 0;
    int n = out->len < target_len ? out->len : target_len;
    for (int i = 0; i < n; i++) {
        if (out->values[i] == target[i]) matches++;
        else break;
    }
    double score = target_len > 0 ? (double)matches / target_len : 0.0;
    return score - plen * 0.004;
}

typedef struct {
    double f;
    int idx;
} ScoredIdx;

static int cmp_scored(const void *a, const void *b) {
    double fa = ((const ScoredIdx *)a)->f;
    double fb = ((const ScoredIdx *)b)->f;
    if (fa < fb) return 1;
    if (fa > fb) return -1;
    return 0;
}

static bool prog_eq(const Individual *a, const Individual *b) {
    if (a->len != b->len) return false;
    return memcmp(a->code, b->code, a->len) == 0;
}

static void random_individual(Individual *ind, Rng *rng, int lo, int hi) {
    int L = rand_len(rng, lo, hi);
    for (int j = 0; j < L; j++) ind->code[j] = (unsigned char)rng_below(rng, N_OPS);
    ind->len = L;
}

MetaResult metabiology(
    const int *target, int target_len,
    int pop_size, int generations, long eval_budget,
    double mutation_rate, double xover_prob, double elite_frac, double inject_frac,
    Individual *initial_pop, int initial_pop_len,
    bool verbose, Rng *rng
) {
    MetaResult mr;
    memset(&mr, 0, sizeof(mr));
    mr.has_best_program = false;
    mr.best_fitness = -1.0;

    Individual *pop = malloc(sizeof(Individual) * (size_t)pop_size);
    Individual *new_pop = malloc(sizeof(Individual) * (size_t)pop_size);
    ScoredIdx *scored = malloc(sizeof(ScoredIdx) * (size_t)pop_size);
    VMResult *outs = malloc(sizeof(VMResult) * (size_t)pop_size);
    GenRecord *history = malloc(sizeof(GenRecord) * (size_t)generations);
    bool *counted = malloc(sizeof(bool) * (size_t)pop_size);

    if (!pop || !new_pop || !scored || !outs || !history || !counted) {
        fprintf(stderr, "metabiology: allocation failed\n");
        free(pop); free(new_pop); free(scored); free(outs); free(history); free(counted);
        return mr;
    }

    if (initial_pop != NULL) {
        int i = 0;
        for (; i < initial_pop_len && i < pop_size; i++) pop[i] = initial_pop[i];
        for (; i < pop_size; i++) random_individual(&pop[i], rng, 3, 10);
    } else {
        for (int i = 0; i < pop_size; i++) random_individual(&pop[i], rng, 3, 10);
    }

    clock_t t0 = clock();
    int history_len = 0;
    int gen;

    for (gen = 0; gen < generations; gen++) {
        for (int i = 0; i < pop_size; i++) {
            long steps_used;
            outs[i] = run_vm(pop[i].code, pop[i].len, eval_budget, &steps_used);
            scored[i].f = fitness_fn(&outs[i], target, target_len, pop[i].len);
            scored[i].idx = i;
        }
        qsort(scored, (size_t)pop_size, sizeof(ScoredIdx), cmp_scored);

        int best_idx = scored[0].idx;
        double best_f = scored[0].f;
        VMResult *best_out = &outs[best_idx];

        if (best_f > mr.best_fitness) {
            mr.best_fitness = best_f;
            memcpy(mr.best_program, pop[best_idx].code, (size_t)pop[best_idx].len);
            mr.best_program_len = pop[best_idx].len;
            mr.has_best_program = true;
            mr.best_output_len = best_out->len < MAX_OUT ? best_out->len : MAX_OUT;
            memcpy(mr.best_output, best_out->values, (size_t)mr.best_output_len * sizeof(int));
        }

        double sum_f = 0.0;
        for (int i = 0; i < pop_size; i++) sum_f += scored[i].f;
        double avg_f = sum_f / pop_size;

        memset(counted, 0, sizeof(bool) * (size_t)pop_size);
        int unique_count = 0;
        for (int i = 0; i < pop_size; i++) {
            if (counted[i]) continue;
            unique_count++;
            for (int j = i + 1; j < pop_size; j++) {
                if (!counted[j] && prog_eq(&pop[i], &pop[j])) counted[j] = true;
            }
        }
        double diversity = (double)unique_count / pop_size;

        GenRecord *gr = &history[history_len++];
        gr->gen = gen;
        gr->best_fitness = best_f;
        int match_len = best_out->len < target_len ? best_out->len : target_len;
        gr->best_output_len = match_len < MAX_OUT ? match_len : MAX_OUT;
        memcpy(gr->best_output, best_out->values, (size_t)gr->best_output_len * sizeof(int));
        gr->best_len = pop[best_idx].len;
        gr->avg_fitness = avg_f;
        gr->diversity = diversity;

        if (verbose && (gen % 50 == 0 || best_f >= 1.0 - 1e-9)) {
            printf("gen %d fit=%.3f out=[", gen, best_f);
            for (int i = 0; i < gr->best_output_len; i++)
                printf("%d%s", gr->best_output[i], i + 1 < gr->best_output_len ? ", " : "");
            printf("] len=%d div=%.2f\n", gr->best_len, diversity);
        }

        bool solved_now = (match_len == target_len);
        if (solved_now) {
            for (int i = 0; i < target_len; i++) {
                if (gr->best_output[i] != target[i]) { solved_now = false; break; }
            }
        }
        if (solved_now) {
            if (verbose) printf("Solved at generation %d\n", gen);
            break;
        }

        int n_elite = (int)(pop_size * elite_frac);
        if (n_elite < 2) n_elite = 2;

        for (int i = 0; i < n_elite; i++) new_pop[i] = pop[scored[i].idx];

        int filled = n_elite;
        while (filled < pop_size) {
            int p1 = rng_below(rng, n_elite);
            unsigned char child[MAX_PROG_LEN];
            int child_len;
            double r = rng_double(rng);
            if (r < xover_prob && n_elite >= 2) {
                int p2 = rng_below(rng, n_elite);
                child_len = crossover_prog(new_pop[p1].code, new_pop[p1].len,
                                            new_pop[p2].code, new_pop[p2].len, child, rng);
            } else {
                memcpy(child, new_pop[p1].code, (size_t)new_pop[p1].len);
                child_len = new_pop[p1].len;
            }
            unsigned char mutated[MAX_PROG_LEN];
            int mlen = mutate_prog(child, child_len, mutation_rate, mutated, rng);
            memcpy(new_pop[filled].code, mutated, (size_t)mlen);
            new_pop[filled].len = mlen;
            filled++;
        }

        int n_inj = (int)(pop_size * inject_frac);
        if (n_inj < 1) n_inj = 1;
        for (int i = 0; i < n_inj; i++) {
            int idx = pop_size - 1 - i;
            if (idx < 0) break;
            random_individual(&new_pop[idx], rng, 3, 12);
        }

        memcpy(pop, new_pop, sizeof(Individual) * (size_t)pop_size);
    }

    mr.solved = false;
    if (mr.has_best_program && mr.best_output_len >= target_len) {
        mr.solved = true;
        for (int i = 0; i < target_len; i++) {
            if (mr.best_output[i] != target[i]) { mr.solved = false; break; }
        }
    }
    mr.history = history;
    mr.history_len = history_len;
    mr.wall_time = (double)(clock() - t0) / CLOCKS_PER_SEC;

    free(pop);
    free(new_pop);
    free(scored);
    free(outs);
    free(counted);

    return mr;
}

void metabiology_free(MetaResult *mr) {
    free(mr->history);
    mr->history = NULL;
    mr->history_len = 0;
}
