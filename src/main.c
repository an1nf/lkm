#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "levin.h"
#include "metabiology.h"
#include "rng.h"

static void print_output(const int *values, int len) {
    printf("[");
    for (int i = 0; i < len; i++) printf("%d%s", values[i], i + 1 < len ? ", " : "");
    printf("]");
}

static void demo_levin(Rng *rng) {
    printf("\nDEMO 1: Levin Search - finding shortest programs\n");
    printf("Budget for program of length L in phase k: floor(k / 2^L)\n");
    printf("Exhaustive for L<=4, stochastic sampling for L>=5\n");

    typedef struct { const char *name; int target[4]; int len; } TgtEntry;
    TgtEntry targets[] = {
        {"constant [3]", {3},    1},
        {"pair [1,2]",   {1, 2}, 2},
        {"zeros [0,0]",  {0, 0}, 2},
        {"sum [5]",      {5},    1},
    };
    int n_targets = 4;

    for (int t = 0; t < n_targets; t++) {
        printf("\nTarget: %s\n", targets[t].name);
        LResult r = levin_search(targets[t].target, targets[t].len, 6, 2000000, false, rng);
        if (r.found) {
            char hex[256];
            hex_repr(r.program, r.prog_len, hex, sizeof(hex));
            const char *tag = r.stochastic ? "stochastic" : "exact";
            printf("Found: length %d, %ld programs tested, %ld budget units, %.2fs (%s)\n",
                   r.result_len, r.programs, r.steps, r.wall_time, tag);
            printf("Hex: %s\n", hex);
            disassemble(r.program, r.prog_len);
            long steps_used;
            VMResult verify = run_vm(r.program, r.prog_len, 10000, &steps_used);
            printf("Output: ");
            print_output(verify.values, verify.len);
            printf("\n");
        } else {
            printf("Budget exhausted: %ld steps, %ld programs tested\n", r.steps, r.programs);
        }
    }

    printf("\nVerification: [0,0,0] is encodable\n");
    unsigned char prog_000[] = {9, 1, 9, 1, 9, 1};
    long steps_used;
    VMResult out = run_vm(prog_000, 6, 100, &steps_used);
    char hex[64];
    hex_repr(prog_000, 6, hex, sizeof(hex));
    printf("Program: %s (6 bytes)\n", hex);
    disassemble(prog_000, 6);
    printf("Output: ");
    print_output(out.values, out.len);
    printf("\n");
    long total16_6 = ipow(16, 6);
    printf("Search space at L=6: 16^6 = %ld candidates, P(hit)~%.6f per phase\n",
           total16_6, 6000.0 / (double)total16_6);
}

static void demo_kolmogorov(Rng *rng) {
    printf("\nDEMO 2: Kolmogorov complexity upper bounds\n");
    printf("K(x) = length of shortest program that outputs x, uncomputable in general\n");

    typedef struct { const char *name; int data[4]; int len; } SeqEntry;
    SeqEntry sequences[] = {
        {"[0]",   {0},    1},
        {"[3]",   {3},    1},
        {"[0,0]", {0, 0}, 2},
        {"[1,2]", {1, 2}, 2},
        {"[5]",   {5},    1},
        {"[2,3]", {2, 3}, 2},
    };
    int n_seq = 6;

    for (int i = 0; i < n_seq; i++) {
        KResult k = kolmogorov_upper(sequences[i].data, sequences[i].len, 6, 800000, rng);
        char phex[256];
        if (k.has_program) hex_repr(k.program, k.prog_len, phex, sizeof(phex));
        else snprintf(phex, sizeof(phex), "not found");

        if (k.K_upper >= 0) {
            printf("%-8s K<=%d  |x|=%d  ratio=%.2f  program=%s\n",
                   sequences[i].name, k.K_upper, sequences[i].len, k.ratio, phex);
        } else {
            printf("%-8s K<=inf  |x|=%d  program=%s\n",
                   sequences[i].name, sequences[i].len, phex);
        }
    }
    printf("ratio<1: compression. ratio=1: no gain. ratio>1: worse than raw data.\n");
}

static void demo_neutral_evolution(void) {
    printf("\nDEMO 3: Neutral evolution - topology of program space\n");

    int target[] = {1, 2};
    printf("Enumerating programs of length <=4 that output [1,2]\n");

    const int capacity = 20000;
    unsigned char (*neutral_set)[MAX_PROG_LEN] = malloc((size_t)capacity * MAX_PROG_LEN);
    int *neutral_lens = malloc((size_t)capacity * sizeof(int));
    if (!neutral_set || !neutral_lens) {
        fprintf(stderr, "demo_neutral_evolution: allocation failed\n");
        free(neutral_set);
        free(neutral_lens);
        return;
    }
    int neutral_count = 0;

    for (int L = 1; L <= 4; L++) {
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
            long steps_used;
            VMResult out = run_vm(tup, L, 200, &steps_used);
            if (out.ok && out.len >= 2 && out.values[0] == target[0] && out.values[1] == target[1]) {
                if (neutral_count < capacity) {
                    memcpy(neutral_set[neutral_count], tup, L);
                    neutral_lens[neutral_count] = L;
                    neutral_count++;
                }
            }
        }
    }

    printf("Found %d neutral programs. Sample:\n", neutral_count);
    int show = neutral_count < 14 ? neutral_count : 14;
    for (int i = 0; i < show; i++) {
        long steps_used;
        VMResult out = run_vm(neutral_set[i], neutral_lens[i], 200, &steps_used);
        char hex[64];
        hex_repr(neutral_set[i], neutral_lens[i], hex, sizeof(hex));
        printf("  %-24s -> ", hex);
        int m = out.len < 4 ? out.len : 4;
        print_output(out.values, m);
        printf("\n");
    }
    printf("Same-output programs form a neutral network; single-byte mutations drift\n");
    printf("between them, occasionally crossing into a higher fitness peak.\n");

    free(neutral_set);
    free(neutral_lens);
}

static void print_trajectory(GenRecord *history, int history_len, int step) {
    for (int i = 0; i < history_len; i += step) {
        GenRecord *h = &history[i];
        printf("gen %4d  best=%.3f  avg=%.3f  div=%.2f  out=",
               h->gen, h->best_fitness, h->avg_fitness, h->diversity);
        print_output(h->best_output, h->best_output_len);
        printf("\n");
    }
}

static void demo_metabiology(Rng *rng) {
    printf("\nDEMO 4: Metabiology - local optima and escape in program space\n");

    int target_easy[] = {1, 2, 3};
    printf("\nPhase A: easy target [1,2,3]\n");

    MetaResult r_easy = metabiology(target_easy, 3, 200, 400, 800, 0.15, 0.2, 0.3, 0.1,
                                     NULL, 0, false, rng);
    if (r_easy.has_best_program) {
        printf("Best fitness=%.4f\n", r_easy.best_fitness);
        char hex[256];
        hex_repr(r_easy.best_program, r_easy.best_program_len, hex, sizeof(hex));
        printf("%s\n", hex);
        disassemble(r_easy.best_program, r_easy.best_program_len);
        long steps_used;
        VMResult out = run_vm(r_easy.best_program, r_easy.best_program_len, 10000, &steps_used);
        printf("Output: ");
        print_output(out.values, out.len < 8 ? out.len : 8);
        printf("  solved=%s\n", r_easy.solved ? "true" : "false");
    }

    int step = r_easy.history_len / 8;
    if (step < 1) step = 1;
    printf("Trajectory (every %d gen):\n", step);
    print_trajectory(r_easy.history, r_easy.history_len, step);
    metabiology_free(&r_easy);

    int target_hard[] = {1, 2, 3, 4, 5};
    printf("\nPhase B: hard target [1,2,3,4,5], standard run\n");

    MetaResult r_hard = metabiology(target_hard, 5, 200, 350, 1000, 0.12, 0.2, 0.3, 0.1,
                                     NULL, 0, false, rng);
    printf("Stuck at fitness=%.4f  output=", r_hard.best_fitness);
    print_output(r_hard.best_output, r_hard.best_output_len);
    printf("  solved=%s\n", r_hard.solved ? "true" : "false");
    metabiology_free(&r_hard);

    printf("\nPhase C: seeded from loop architecture\n");

    unsigned char loop_seed[] = {9, 7, 2, 1, 2, 12, 5, 5, 15, 247};
    int loop_len = 10;
    long steps_used;
    VMResult loop_out = run_vm(loop_seed, loop_len, 500, &steps_used);
    char hexloop[64];
    hex_repr(loop_seed, loop_len, hexloop, sizeof(hexloop));
    printf("Loop seed (%d bytes): %s\n", loop_len, hexloop);
    disassemble(loop_seed, loop_len);
    printf("Output: ");
    print_output(loop_out.values, loop_out.len);
    printf("\n");

    int seed_pop_size = 200;
    Individual *seed_pop = malloc(sizeof(Individual) * (size_t)seed_pop_size);
    if (!seed_pop) {
        fprintf(stderr, "demo_metabiology: allocation failed\n");
        return;
    }
    for (int i = 0; i < seed_pop_size; i++) {
        double rate = 0.08 + rng_double(rng) * (0.25 - 0.08);
        unsigned char mutated[MAX_PROG_LEN];
        int mlen = mutate_prog(loop_seed, loop_len, rate, mutated, rng);
        memcpy(seed_pop[i].code, mutated, (size_t)mlen);
        seed_pop[i].len = mlen;
    }

    MetaResult r_escape = metabiology(target_hard, 5, 200, 300, 1500, 0.12, 0.20, 0.30, 0.05,
                                       seed_pop, seed_pop_size, false, rng);
    free(seed_pop);

    printf("After escape: fitness=%.4f  solved=%s\n",
           r_escape.best_fitness, r_escape.solved ? "true" : "false");
    if (r_escape.has_best_program) {
        char hex[256];
        hex_repr(r_escape.best_program, r_escape.best_program_len, hex, sizeof(hex));
        printf("%s\n", hex);
        disassemble(r_escape.best_program, r_escape.best_program_len);
        VMResult out_e = run_vm(r_escape.best_program, r_escape.best_program_len, 10000, &steps_used);
        printf("Output: ");
        print_output(out_e.values, out_e.len < 10 ? out_e.len : 10);
        printf("\n");
    }
    metabiology_free(&r_escape);

    printf("\nStraight-line programs (PUSH OUT PUSH OUT...) plateau at short prefixes.\n");
    printf("Loop-based programs (PUSH0 INC DUP OUT... JNZ) generalize to any length\n");
    printf("by changing one operand. Evolution cannot bridge the two by gradient;\n");
    printf("crossing requires a random qualitative jump, so K-complexity growth\n");
    printf("in Chaitin's metabiology is not smooth.\n");
}

int main(void) {
    printf("Levin Search / Kolmogorov Complexity / Metabiology\n");
    printf("VM: %d opcodes, byte-encoded programs, INT_CAP=%d\n", N_OPS, INT_CAP);

    Rng rng;
    rng_seed(&rng, 42, 54);

    demo_levin(&rng);
    demo_kolmogorov(&rng);
    demo_neutral_evolution();
    demo_metabiology(&rng);

    printf("\nComplete.\n");

    return 0;
}
