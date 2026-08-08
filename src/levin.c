#include "levin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

long ipow(long base, int exp) {
    long r = 1;
    for (int i = 0; i < exp; i++) r *= base;
    return r;
}

bool next_tuple(unsigned char *tup, int L, int base) {
    for (int i = L - 1; i >= 0; i--) {
        tup[i]++;
        if (tup[i] < base) return true;
        tup[i] = 0;
    }
    return false;
}

bool output_matches_target(const VMResult *out, const int *target, int target_len) {
    if (!out->ok) return false;
    if (out->len < target_len) return false;
    for (int i = 0; i < target_len; i++) {
        if (out->values[i] != target[i]) return false;
    }
    return true;
}

static long tuple_index(const unsigned char *tup, int L) {
    long idx = 0;
    for (int i = 0; i < L; i++) idx = idx * N_OPS + tup[i];
    return idx;
}

/*
 * Presupuesto ya consumido por cada programa candidato, indexado
 * directamente por su valor en base N_OPS. Un array separado por
 * longitud evita colisiones de hash y la asignación de un nodo
 * por entrada; se reserva una sola vez por longitud activa y se
 * libera al terminar la búsqueda.
 */
typedef struct {
    long *budget[LEVIN_MAX_ENUM_LEN + 1];
} ConsumedTable;

static void consumed_init(ConsumedTable *ct) {
    for (int L = 0; L <= LEVIN_MAX_ENUM_LEN; L++) ct->budget[L] = NULL;
}

static void consumed_ensure(ConsumedTable *ct, int L) {
    if (L < 1 || L > LEVIN_MAX_ENUM_LEN) return;
    if (ct->budget[L] != NULL) return;
    long size = ipow(N_OPS, L);
    ct->budget[L] = calloc((size_t)size, sizeof(long));
}

static long consumed_get(ConsumedTable *ct, int L, const unsigned char *tup) {
    if (L < 1 || L > LEVIN_MAX_ENUM_LEN || ct->budget[L] == NULL) return 0;
    return ct->budget[L][tuple_index(tup, L)];
}

static void consumed_set(ConsumedTable *ct, int L, const unsigned char *tup, long value) {
    if (L < 1 || L > LEVIN_MAX_ENUM_LEN || ct->budget[L] == NULL) return;
    ct->budget[L][tuple_index(tup, L)] = value;
}

static void consumed_free(ConsumedTable *ct) {
    for (int L = 0; L <= LEVIN_MAX_ENUM_LEN; L++) {
        free(ct->budget[L]);
        ct->budget[L] = NULL;
    }
}

static void fill_lresult_running(LResult *r, long used, long tested, long phase_n,
                                  clock_t t0, bool stochastic) {
    r->found = false;
    r->steps = used;
    r->programs = tested;
    r->phases = phase_n;
    r->wall_time = (double)(clock() - t0) / CLOCKS_PER_SEC;
    r->stochastic = stochastic;
}

static void fill_lresult_found(LResult *r, const unsigned char *prog, int L,
                                const VMResult *out, long used, long tested,
                                long phase_n, clock_t t0, bool stochastic) {
    memcpy(r->program, prog, L);
    r->prog_len = L;
    r->found = true;
    r->result_len = L;
    r->output_len = out->len < MAX_OUT ? out->len : MAX_OUT;
    memcpy(r->output, out->values, r->output_len * sizeof(int));
    r->steps = used;
    r->programs = tested;
    r->phases = phase_n;
    r->wall_time = (double)(clock() - t0) / CLOCKS_PER_SEC;
    r->stochastic = stochastic;
}

LResult levin_search(const int *target, int target_len, int max_len,
                      long total_budget, bool verbose, Rng *rng) {
    LResult r;
    memset(&r, 0, sizeof(r));
    r.found = false;
    r.stochastic = false;

    ConsumedTable ct;
    consumed_init(&ct);

    long used = 0, tested = 0, phase_n = 0, phase = 1;
    clock_t t0 = clock();

    while (used < total_budget) {
        phase_n++;
        int active_max = max_len;
        int log2phase = (int)(log((double)(phase > 0 ? phase : 1)) / log(2.0)) + 1;
        if (log2phase < active_max) active_max = log2phase;

        if (verbose) {
            double elapsed = (double)(clock() - t0) / CLOCKS_PER_SEC;
            printf("phase %ld budget=%ld len<=%d steps=%ld/%ld t=%.1fs\n",
                   phase_n, phase, active_max, used, total_budget, elapsed);
        }

        for (int L = 1; L <= active_max; L++) {
            long alloc = phase >> L;
            if (alloc < 1) continue;

            long n_progs = ipow(N_OPS, L);

            if (n_progs <= ENUM_LIMIT && L <= LEVIN_MAX_ENUM_LEN) {
                consumed_ensure(&ct, L);

                unsigned char tup[MAX_PROG_LEN];
                memset(tup, 0, sizeof(tup));
                bool has_more = true, first = true;

                while (has_more) {
                    if (!first) {
                        has_more = next_tuple(tup, L, N_OPS);
                        if (!has_more) break;
                    } else {
                        first = false;
                    }

                    long prev = consumed_get(&ct, L, tup);
                    if (alloc <= prev) continue;

                    long steps_used;
                    VMResult out = run_vm(tup, L, alloc, &steps_used);
                    used += alloc - prev;
                    tested++;
                    consumed_set(&ct, L, tup, alloc);

                    if (output_matches_target(&out, target, target_len)) {
                        fill_lresult_found(&r, tup, L, &out, used, tested, phase_n, t0, false);
                        consumed_free(&ct);
                        return r;
                    }
                    if (used >= total_budget) {
                        fill_lresult_running(&r, used, tested, phase_n, t0, false);
                        consumed_free(&ct);
                        return r;
                    }
                }
            } else {
                r.stochastic = true;
                long n_samples = 6000;
                if (total_budget - used < n_samples) n_samples = total_budget - used;

                for (long s = 0; s < n_samples; s++) {
                    unsigned char tup[MAX_PROG_LEN];
                    for (int i = 0; i < L; i++) tup[i] = (unsigned char)rng_below(rng, N_OPS);

                    long steps_used;
                    VMResult out = run_vm(tup, L, alloc, &steps_used);
                    used += alloc;
                    tested++;

                    if (output_matches_target(&out, target, target_len)) {
                        fill_lresult_found(&r, tup, L, &out, used, tested, phase_n, t0, true);
                        consumed_free(&ct);
                        return r;
                    }
                    if (used >= total_budget) {
                        fill_lresult_running(&r, used, tested, phase_n, t0, true);
                        consumed_free(&ct);
                        return r;
                    }
                }
            }
        }

        phase <<= 1;
    }

    fill_lresult_running(&r, used, tested, phase_n, t0, r.stochastic);
    consumed_free(&ct);
    return r;
}

KResult kolmogorov_upper(const int *data, int data_len, int max_len,
                          long total_budget, Rng *rng) {
    KResult k;
    memset(&k, 0, sizeof(k));

    LResult r = levin_search(data, data_len, max_len, total_budget, false, rng);
    if (r.found) {
        k.K_upper = r.result_len;
        k.ratio = (double)r.result_len / (data_len > 0 ? data_len : 1);
        memcpy(k.program, r.program, r.prog_len);
        k.prog_len = r.prog_len;
        k.has_program = true;
        k.steps = r.steps;
    } else {
        k.K_upper = -1;
        k.ratio = -1.0;
        k.has_program = false;
    }
    return k;
}
