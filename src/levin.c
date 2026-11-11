#include "levin.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
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

static uint64_t tuple_index(const unsigned char *tup, int L) {
    uint64_t idx = 0;
    for (int i = 0; i < L; i++) idx = (idx << SYMBOL_BITS) | (tup[i] & 15);
    return idx;
}

static double seconds_since(clock_t t0) {
    return (double)(clock() - t0) / CLOCKS_PER_SEC;
}

LResult levin_search_pred(ProgPred pred, void *ctx, int max_len,
                          long total_budget, bool verbose) {
    LResult r;
    memset(&r, 0, sizeof(r));
    if (max_len > LEVIN_MAX_LEN) max_len = LEVIN_MAX_LEN;

    /* resolved[L]: bit i = el programa i de longitud L ya paró sin éxito */
    uint64_t *resolved[LEVIN_MAX_LEN + 1] = {0};

    long used = 0, tested = 0, phase_n = 0;
    clock_t t0 = clock();

    for (int k = 0; k < 62 && used < total_budget; k++) {
        long T = 1L << k;
        phase_n++;
        if (verbose) {
            printf("  phase %2d  T=2^%d  len<=%d  steps=%ld  t=%.2fs\n",
                   k, k, k / SYMBOL_BITS < max_len ? k / SYMBOL_BITS : max_len,
                   used, seconds_since(t0));
        }

        for (int L = 1; L <= max_len; L++) {
            if (SYMBOL_BITS * L > k) break;                  /* T / 16^L < 1 */
            long alloc = T >> (SYMBOL_BITS * L);

            if (L <= LEVIN_BITMAP_MAX_LEN && resolved[L] == NULL) {
                size_t words = ((size_t)1 << (SYMBOL_BITS * L)) / 64 + 1;
                resolved[L] = calloc(words, sizeof(uint64_t));
            }
            uint64_t *bm = (L <= LEVIN_BITMAP_MAX_LEN) ? resolved[L] : NULL;

            unsigned char tup[MAX_PROG_LEN];
            memset(tup, 0, sizeof(tup));
            do {
                uint64_t idx = 0;
                if (bm) {
                    idx = tuple_index(tup, L);
                    if (bm[idx >> 6] & (1ULL << (idx & 63))) continue;
                }
                long s = 0;
                VMResult out;
                bool hit = pred(tup, L, alloc, &s, &out, ctx);
                used += s;
                tested++;
                if (hit) {
                    memcpy(r.program, tup, (size_t)L);
                    r.prog_len = L;
                    r.found = true;
                    r.output = out;
                    goto done;
                }
                if (bm && out.status != VM_TIMEOUT) bm[idx >> 6] |= 1ULL << (idx & 63);
                if (used >= total_budget) goto done;
            } while (next_tuple(tup, L, N_OPS));
        }
    }

done:
    r.steps = used;
    r.programs = tested;
    r.phases = phase_n;
    r.wall_time = seconds_since(t0);
    for (int L = 0; L <= LEVIN_MAX_LEN; L++) free(resolved[L]);
    return r;
}

/* ---------- predicados ---------- */

typedef struct {
    const int *target;
    int len;
    bool exact;   /* exact: salida == target y parada limpia; si no, prefijo */
} TargetCtx;

static bool pred_target(const unsigned char *code, int n, long budget,
                        long *steps_used, VMResult *out, void *ctx) {
    TargetCtx *tc = ctx;
    *out = run_vm(code, n, budget, steps_used);
    if (tc->exact) {
        return out->status == VM_HALT && out->len == tc->len &&
               output_matches_target(out, tc->target, tc->len);
    }
    return output_matches_target(out, tc->target, tc->len);
}

bool pred_quine(const unsigned char *code, int n, long budget,
                long *steps_used, VMResult *out, void *ctx) {
    (void)ctx;
    int expect[MAX_PROG_LEN];
    for (int i = 0; i < n; i++) expect[i] = code[i] & 15;
    *out = run_vm_expect(code, n, budget, steps_used, expect, n);
    return out->status == VM_HALT && out->len == n;
}

bool pred_reverse_quine(const unsigned char *code, int n, long budget,
                        long *steps_used, VMResult *out, void *ctx) {
    (void)ctx;
    int expect[MAX_PROG_LEN];
    for (int i = 0; i < n; i++) expect[i] = code[n - 1 - i] & 15;
    *out = run_vm_expect(code, n, budget, steps_used, expect, n);
    return out->status == VM_HALT && out->len == n;
}

LResult levin_search(const int *target, int target_len, int max_len,
                     long total_budget, bool verbose) {
    TargetCtx tc = {target, target_len, false};
    return levin_search_pred(pred_target, &tc, max_len, total_budget, verbose);
}

KResult kolmogorov_upper(const int *data, int data_len, int max_len, long total_budget) {
    KResult k;
    memset(&k, 0, sizeof(k));
    TargetCtx tc = {data, data_len, true};
    LResult r = levin_search_pred(pred_target, &tc, max_len, total_budget, false);
    if (r.found) {
        k.K_upper = r.prog_len;
        k.K_bits = SYMBOL_BITS * r.prog_len;
        k.ratio = (double)r.prog_len / (data_len > 0 ? data_len : 1);
        memcpy(k.program, r.program, (size_t)r.prog_len);
        k.prog_len = r.prog_len;
        k.has_program = true;
        k.steps = r.steps;
    } else {
        k.K_upper = -1;
        k.K_bits = -1;
        k.ratio = -1.0;
    }
    return k;
}
