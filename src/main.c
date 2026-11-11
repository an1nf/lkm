#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "vm.h"
#include "levin.h"
#include "metabiology.h"
#include "selfref.h"
#include "rng.h"

/* ------------------------------------------------------------------ */

static void print_output(const int *values, int len) {
    printf("[");
    for (int i = 0; i < len; i++) printf("%d%s", values[i], i + 1 < len ? ", " : "");
    printf("]");
}

static void print_prog(const unsigned char *code, int n) {
    char hex[MAX_PROG_LEN + 1];
    hex_repr(code, n, hex, sizeof(hex));
    printf("%s", hex);
}

static void header(const char *title) {
    printf("\n================================================================\n");
    printf("%s\n", title);
    printf("================================================================\n");
}

/* ------------------------------------------------------------------ */
/* DEMO 1: búsqueda de Levin                                           */
/* ------------------------------------------------------------------ */

static void demo_levin(void) {
    header("DEMO 1: Levin search (Kraft-weighted: each program gets T * 16^-L steps)");

    typedef struct { const char *name; int target[8]; int len; } TgtEntry;
    TgtEntry targets[] = {
        {"[3]",       {3},       1},
        {"[1,2]",     {1, 2},    2},
        {"[0,0]",     {0, 0},    2},
        {"[2,3]",     {2, 3},    2},
        {"[1,2,3]",   {1, 2, 3}, 3},
        {"[7,7,7,7]", {7, 7, 7, 7}, 4},
    };
    int n_targets = (int)(sizeof(targets) / sizeof(targets[0]));

    for (int t = 0; t < n_targets; t++) {
        printf("\nTarget (output starts with): %s\n", targets[t].name);
        LResult r = levin_search(targets[t].target, targets[t].len, 7, 2000000000L, false);
        if (r.found) {
            printf("  found: %d symbols (%d bits), phase 2^%ld, %ld runs, %ld VM steps, %.2fs\n",
                   r.prog_len, SYMBOL_BITS * r.prog_len, r.phases - 1, r.programs, r.steps,
                   r.wall_time);
            printf("  program: ");
            print_prog(r.program, r.prog_len);
            printf("   output: ");
            print_output(r.output.values, r.output.len < 8 ? r.output.len : 8);
            printf("%s\n", r.output.len > 8 ? "..." : "");
            disassemble(r.program, r.prog_len);
        } else {
            printf("  budget exhausted: %ld steps, %ld runs\n", r.steps, r.programs);
        }
    }
    printf("\nLevin minimises Kt(p) = |p| + log2(t), not |p| alone: a longer but\n");
    printf("faster program can win. The cost of finding a program of L symbols\n");
    printf("is ~16^L * t steps, so L=10 already needs ~10^12 * t.\n");
}

/* ------------------------------------------------------------------ */
/* DEMO 2: cotas de Kolmogorov                                         */
/* ------------------------------------------------------------------ */

static void demo_kolmogorov(void) {
    header("DEMO 2: Kolmogorov complexity upper bounds (exact output, clean halt)");

    typedef struct { const char *name; int data[8]; int len; } SeqEntry;
    SeqEntry seqs[] = {
        {"[0]",        {0},          1},
        {"[3]",        {3},          1},
        {"[15]",       {15},         1},
        {"[0,0]",      {0, 0},       2},
        {"[1,2]",      {1, 2},       2},
        {"[2,3]",      {2, 3},       2},
        {"[1,2,3]",    {1, 2, 3},    3},
    };
    int n = (int)(sizeof(seqs) / sizeof(seqs[0]));

    printf("%-11s %-6s %-7s %-5s %s\n", "x", "K<=", "bits", "|x|", "program");
    for (int i = 0; i < n; i++) {
        KResult k = kolmogorov_upper(seqs[i].data, seqs[i].len, 7, 1500000000L);
        if (k.has_program) {
            printf("%-11s %-6d %-7d %-5d ", seqs[i].name, k.K_upper, k.K_bits, seqs[i].len);
            print_prog(k.program, k.prog_len);
            printf("\n");
        } else {
            printf("%-11s %-6s %-7s %-5d not found within budget\n",
                   seqs[i].name, "?", "?", seqs[i].len);
        }
    }
    printf("Each output value is a 4-bit symbol, so |x| symbols = 4|x| bits of raw data.\n");
    printf("These are upper bounds for *this* machine; K is uncomputable in general.\n");
    printf("Compare DEMO 1: a program whose output merely *starts with* [1,2,3]\n");
    printf("(the endless counter 9721dd, 6 symbols) is shorter than any program that\n");
    printf("prints exactly [1,2,3] and halts (> 7 symbols): monotone vs plain complexity.\n");
}

/* ------------------------------------------------------------------ */
/* DEMO 3: redes neutrales                                             */
/* ------------------------------------------------------------------ */

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static long find_u32(const uint32_t *arr, long n, uint32_t key) {
    long lo = 0, hi = n - 1;
    while (lo <= hi) {
        long mid = (lo + hi) / 2;
        if (arr[mid] == key) return mid;
        if (arr[mid] < key) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

static long uf_find(long *parent, long x) {
    while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
    return x;
}

static void demo_neutral(void) {
    header("DEMO 3: Neutral networks (programs of length L whose output starts with [1,2])");
    int target[] = {1, 2};

    printf("%-3s %-10s %-11s %-12s %-10s %s\n", "L", "programs", "fraction", "robustness",
           "components", "largest");
    for (int L = 5; L <= 7; L++) {
        uint32_t total = 1u << (SYMBOL_BITS * L);
        long cap = 1 << 16, count = 0;
        uint32_t *set = malloc(sizeof(uint32_t) * (size_t)cap);
        for (uint32_t idx = 0; idx < total; idx++) {
            unsigned char tup[MAX_PROG_LEN];
            uint32_t x = idx;
            for (int i = L - 1; i >= 0; i--) { tup[i] = x & 15; x >>= 4; }
            long steps;
            VMResult out = run_vm(tup, L, 256, &steps);
            if (output_matches_target(&out, target, 2)) {
                if (count == cap) { cap *= 2; set = realloc(set, sizeof(uint32_t) * (size_t)cap); }
                set[count++] = idx;
            }
        }
        qsort(set, (size_t)count, sizeof(uint32_t), cmp_u32);

        /* Robustez: fracción de mutantes puntuales (15 por posición) que siguen en el conjunto.
         * Componentes: conectividad por mutaciones puntuales dentro del conjunto. */
        long *parent = malloc(sizeof(long) * (size_t)(count > 0 ? count : 1));
        for (long i = 0; i < count; i++) parent[i] = i;
        double robust_sum = 0.0;
        for (long i = 0; i < count; i++) {
            int neutral = 0;
            for (int pos = 0; pos < L; pos++) {
                int shift = SYMBOL_BITS * (L - 1 - pos);
                uint32_t sym = (set[i] >> shift) & 15;
                for (uint32_t v = 0; v < 16; v++) {
                    if (v == sym) continue;
                    uint32_t nb = (set[i] & ~(15u << shift)) | (v << shift);
                    long j = find_u32(set, count, nb);
                    if (j >= 0) {
                        neutral++;
                        long a = uf_find(parent, i), b = uf_find(parent, j);
                        if (a != b) parent[a] = b;
                    }
                }
            }
            robust_sum += neutral / (15.0 * L);
        }
        long comps = 0, largest = 0;
        long *size = calloc((size_t)(count > 0 ? count : 1), sizeof(long));
        for (long i = 0; i < count; i++) size[uf_find(parent, i)]++;
        for (long i = 0; i < count; i++) {
            if (size[i] > 0) comps++;
            if (size[i] > largest) largest = size[i];
        }
        printf("%-3d %-10ld %-11.2e %-12.3f %-10ld %ld\n", L, count, (double)count / total,
               count ? robust_sum / count : 0.0, comps, largest);
        free(size); free(parent); free(set);
    }
    printf("robustness = mean fraction of point mutants that keep the phenotype.\n");
    printf("Robustness is ~10^4 times the fraction: mutants of a solution are far\n");
    printf("more likely to keep the phenotype than random programs. As L grows the\n");
    printf("neutral set gets more robust and its largest component grows, so drift\n");
    printf("can wander further without losing fitness.\n");
}

/* ------------------------------------------------------------------ */
/* DEMO 4: metabiología                                                */
/* ------------------------------------------------------------------ */

static void print_trajectory(GenRecord *history, int history_len, int step) {
    for (int i = 0; i < history_len; i += step) {
        GenRecord *h = &history[i];
        printf("  gen %4d  best=%.3f  avg=%.3f  div=%.2f  out=",
               h->gen, h->best_fitness, h->avg_fitness, h->diversity);
        print_output(h->best_output, h->best_output_len);
        printf("\n");
    }
}

static void demo_metabiology(Rng *rng) {
    header("DEMO 4: Metabiology - can evolution discover loops on its own?");

    int target_easy[] = {1, 2, 3};
    printf("\nPhase A: one run, target [1,2,3] (prefix mode) - a typical plateau\n");
    MetaResult ra = metabiology(target_easy, 3, 200, 400, 1000, 0.15, 0.2, 0.3, 0.1,
                                false, NULL, 0, false, rng);
    printf("  solved=%s  best fitness=%.4f  program=", ra.solved ? "true" : "false",
           ra.best_fitness);
    print_prog(ra.best_program, ra.best_program_len);
    printf("\n");
    int step = ra.history_len / 6;
    print_trajectory(ra.history, ra.history_len, step < 1 ? 1 : step);
    metabiology_free(&ra);

    printf("\nPhase B: hand-made loop seed (new encoding: JNZ offset -7 relative to the jump)\n");
    unsigned char loop_seed[] = {OP_PUSH0, OP_INC, OP_DUP, OP_OUT, OP_DUP, OP_LIT, 5,
                                 OP_SUB, OP_JNZ, 9 /* -7 */};
    int loop_len = 10;
    long steps;
    VMResult lo = run_vm(loop_seed, loop_len, 500, &steps);
    printf("  seed ");
    print_prog(loop_seed, loop_len);
    printf(" -> ");
    print_output(lo.values, lo.len);
    printf("\n");
    disassemble(loop_seed, loop_len);

    printf("  30 runs, exact mode, target [1..10], population = mutants of this seed:\n");
    {
        int t10[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        int solved = 0, loops = 0;
        double gen_sum = 0;
        unsigned char ex[MAX_PROG_LEN];
        int ex_len = 0;
        for (int run = 0; run < 30; run++) {
            Rng r;
            rng_seed(&r, 5000 + (uint64_t)run, 11);
            Individual *seed_pop = malloc(sizeof(Individual) * 200);
            for (int i = 0; i < 200; i++) {
                double rate = 0.05 + rng_double(&r) * 0.15;
                seed_pop[i].len = mutate_prog(loop_seed, loop_len, rate, seed_pop[i].code, &r);
            }
            MetaResult m = metabiology(t10, 10, 200, 1000, 4000, 0.12, 0.2, 0.3, 0.05,
                                       true, seed_pop, 200, false, &r);
            free(seed_pop);
            if (m.solved) {
                solved++;
                gen_sum += m.history_len - 1;
                if (has_backward_jump(m.best_program, m.best_program_len)) loops++;
                if (ex_len == 0 || m.best_program_len < ex_len) {
                    memcpy(ex, m.best_program, (size_t)m.best_program_len);
                    ex_len = m.best_program_len;
                }
            }
            metabiology_free(&m);
        }
        printf("  solved %d/30, %d with loop, mean generation %.1f, shortest ", solved, loops,
               solved ? gen_sum / solved : 0.0);
        if (ex_len) print_prog(ex, ex_len); else printf("-");
        printf("\n");
    }

    printf("\nPhase C: 30 independent runs per cell, random start, no seed, 1000 generations.\n");
    printf("prefix = output must start with the target (endless generators allowed)\n");
    printf("exact  = output must equal the target and the program must halt\n");
    typedef struct { const char *name; int t[16]; int len; } T;
    T targets[] = {
        {"[1..5]",  {1, 2, 3, 4, 5}, 5},
        {"[1..10]", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 10},
        {"[1..15]", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, 15},
    };
    const int RUNS = 30;
    printf("  %-8s %-7s %-7s %-10s %-9s %-9s %s\n", "target", "mode", "solved", "with loop",
           "mean len", "mean gen", "shortest solution");
    for (int mode = 0; mode < 2; mode++) {
        for (int t = 0; t < 3; t++) {
            int solved = 0, loops = 0;
            double len_sum = 0, gen_sum = 0;
            unsigned char ex[MAX_PROG_LEN];
            int ex_len = 0;
            for (int run = 0; run < RUNS; run++) {
                Rng r;
                rng_seed(&r, 1000 + (uint64_t)run, 7);
                MetaResult m = metabiology(targets[t].t, targets[t].len, 200, 1000, 4000,
                                           0.12, 0.2, 0.3, 0.1, mode == 1, NULL, 0, false, &r);
                if (m.solved) {
                    solved++;
                    len_sum += m.best_program_len;
                    gen_sum += m.history_len - 1;
                    if (has_backward_jump(m.best_program, m.best_program_len)) loops++;
                    if (ex_len == 0 || m.best_program_len < ex_len) {
                        memcpy(ex, m.best_program, (size_t)m.best_program_len);
                        ex_len = m.best_program_len;
                    }
                }
                metabiology_free(&m);
            }
            printf("  %-8s %-7s %2d/%-4d %2d/%-7d %-9.1f %-9.1f ", targets[t].name,
                   mode ? "exact" : "prefix", solved, RUNS, loops, solved,
                   solved ? len_sum / solved : 0.0, solved ? gen_sum / solved : 0.0);
            if (ex_len) print_prog(ex, ex_len); else printf("-");
            printf("\n");
            fflush(stdout);
        }
    }
    printf("Before this version, mutation could only emit forward jumps (argument\n");
    printf("bytes were 0..15 but backward offsets needed bytes >= 128), so a loop\n");
    printf("could never evolve. With signed 4-bit offsets the question is empirical.\n");
}

/* ------------------------------------------------------------------ */
/* DEMO 5: parada                                                      */
/* ------------------------------------------------------------------ */

static void demo_halting(int max_len) {
    header("DEMO 5: How programs end, by length (budget 256 steps)");
    printf("%-3s %-12s %-8s %-8s %-8s %-8s %s\n", "L", "programs", "halt", "outfull", "crash",
           "timeout", "time");
    for (int L = 1; L <= max_len; L++) {
        Census *c = malloc(sizeof(Census));
        census_length(L, 256, c);
        double tot = (double)c->total;
        printf("%-3d %-12ld %-8.4f %-8.4f %-8.4f %-8.4f %.2fs\n", L, c->total,
               c->by_status[VM_HALT] / tot, c->by_status[VM_OUT_FULL] / tot,
               c->by_status[VM_CRASH] / tot, c->by_status[VM_TIMEOUT] / tot, c->wall_time);
        free(c);
    }
    printf("'timeout' = did not halt within the budget. Before signed offsets this\n");
    printf("column was identically 0: every program halted in <= L steps.\n");
    printf("Whether a timeout is a true non-halter is the halting problem.\n");
}

/* ------------------------------------------------------------------ */
/* DEMO 6: quines                                                      */
/* ------------------------------------------------------------------ */

static void demo_quines(int max_n) {
    header("DEMO 6: Self-reference - quines and reverse quines");

    printf("\nHand-made quine via self-inspection (READ):\n");
    unsigned char hand[] = {OP_PUSH0, OP_DEC, OP_INC, OP_DUP, OP_READ, OP_DUP, OP_OUT,
                            OP_JNZ, 11 /* -5 */, OP_HALT};
    long steps;
    VMResult out = run_vm(hand, 10, 1000, &steps);
    printf("  code   ");
    print_prog(hand, 10);
    printf("\n  output ");
    for (int i = 0; i < out.len; i++) printf("%x", out.values[i]);
    printf("   (%s, %ld steps)\n", VM_STATUS_NAMES[out.status], steps);
    disassemble(hand, 10);
    printf("  The trailing HALT (0) doubles as data: it is the terminator the loop\n");
    printf("  reads to know it has copied everything. This is Laing-style\n");
    printf("  self-inspection: there is no stored description phi(D) as in\n");
    printf("  von Neumann's design; the program reads its own body.\n");

    for (int map = 0; map < 2; map++) {
        printf("\nExhaustive lazy search, %s, budget 1024 steps:\n",
               map == SELF_IDENTITY ? "QUINES (output == code)" :
                                      "REVERSE QUINES (output == reversed code)");
        printf("  %-3s %-8s %-10s %-12s %-8s %s\n", "n", "count", "no READ", "tree nodes",
               "time", "examples");
        for (int n = 1; n <= max_n; n++) {
            SelfSearch *s = malloc(sizeof(SelfSearch));
            self_search(n, n, (SelfMap)map, 1024, s);
            printf("  %-3d %-8.0f %-10.0f %-12ld %-8.2f", n, s->count, s->count_without_read,
                   s->nodes, s->wall_time);
            for (int i = 0; i < s->n_examples && i < 4; i++) {
                printf(" ");
                print_prog(s->examples[i], n);
            }
            if (s->verify_failures) printf("  [VERIFY FAILURES: %ld]", s->verify_failures);
            printf("\n");
            fflush(stdout);
            free(s);
        }
    }
    printf("\nResults of the full runs (./lkm quines 10, ~25 min on 2 cores):\n");
    printf("  n=9 : 0 quines, 32 reverse quines   n=10: exactly 3 quines\n");
    const char *known[] = {"9273b21fa0", "9728b21fa0", "9872b21fb0", "9b822b1fb", "ac82b12fb"};
    for (int k = 0; k < 5; k++) {
        unsigned char c[MAX_PROG_LEN];
        int n = (int)strlen(known[k]);
        for (int i = 0; i < n; i++) c[i] = (unsigned char)strtol((char[]){known[k][i], 0}, NULL, 16);
        VMResult o = run_vm(c, n, 1024, &steps);
        bool q = o.status == VM_HALT && o.len == n, rq = q;
        for (int i = 0; i < n && (q || rq); i++) {
            if (o.values[i] != c[i]) q = false;
            if (o.values[i] != c[n - 1 - i]) rq = false;
        }
        printf("  %-11s %-14s output ", known[k], q ? "quine" : rq ? "reverse quine" : "??");
        for (int i = 0; i < o.len; i++) printf("%x", o.values[i]);
        printf("\n");
    }
    printf("  9b822b1fb: PUSH0 READ reads code[0] = 9 = its own length.\n");
    printf("  ac82b12fb: JNZ jumps into the operand of LIT 8 and runs it as DEC.\n");

    printf("\n16^n candidates per length, but the lazy search only branches on\n");
    printf("symbols the program executes or READs before printing them: every\n");
    printf("OUT *forces* a symbol, so the tree is far smaller than 16^n.\n");

    printf("\nLevin search for a quine (shows the 2^|p| constant at work):\n");
    LResult r = levin_search_pred(pred_quine, NULL, 8, 400000000L, false);
    if (r.found) {
        printf("  found ");
        print_prog(r.program, r.prog_len);
        printf("\n");
    } else {
        printf("  none within %ld steps (%ld runs, up to length 8). The shortest quine\n",
               r.steps, r.programs);
        printf("  has 10 symbols = 40 bits, so Levin needs ~2^40 * t steps to reach it.\n");
    }
}

/* ------------------------------------------------------------------ */

static void usage(const char *argv0) {
    printf("usage: %s [all|levin|kolmogorov|neutral|metabiology|halting|quines] [max_len]\n",
           argv0);
    printf("  halting max_len: census up to this length (default 7)\n");
    printf("  quines  max_len: quine search up to this length (default 8; 9 takes ~2 min, 10 ~25 min)\n");
}

int main(int argc, char **argv) {
    const char *which = argc > 1 ? argv[1] : "all";
    int arg = argc > 2 ? atoi(argv[2]) : 0;

    printf("lkm: Levin search / Kolmogorov complexity / metabiology / self-reference\n");
    printf("VM: %d opcodes, 4-bit symbols, signed jump offsets -8..+7, READ = self-inspection\n",
           N_OPS);

    Rng rng;
    rng_seed(&rng, 42, 54);
    bool all = strcmp(which, "all") == 0;
    bool any = false;

    if (all || !strcmp(which, "levin"))       { demo_levin(); any = true; }
    if (all || !strcmp(which, "kolmogorov"))  { demo_kolmogorov(); any = true; }
    if (all || !strcmp(which, "neutral"))     { demo_neutral(); any = true; }
    if (all || !strcmp(which, "metabiology")) { demo_metabiology(&rng); any = true; }
    if (all || !strcmp(which, "halting"))     { demo_halting(arg > 0 ? arg : 7); any = true; }
    if (all || !strcmp(which, "quines"))      { demo_quines(arg > 0 ? arg : 8); any = true; }

    if (!any) { usage(argv[0]); return 1; }
    printf("\nComplete.\n");
    return 0;
}
