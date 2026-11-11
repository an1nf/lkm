#ifndef METABIOLOGY_H
#define METABIOLOGY_H

#include <stdbool.h>
#include "vm.h"
#include "rng.h"

typedef struct {
    unsigned char code[MAX_PROG_LEN];
    int len;
} Individual;

typedef struct {
    int gen;
    double best_fitness;
    int best_output[MAX_OUT];
    int best_output_len;
    int best_len;
    double avg_fitness;
    double diversity;
} GenRecord;

typedef struct {
    unsigned char best_program[MAX_PROG_LEN];
    int best_program_len;
    bool has_best_program;
    double best_fitness;
    int best_output[MAX_OUT];
    int best_output_len;
    GenRecord *history;
    int history_len;
    bool solved;
    double wall_time;
} MetaResult;

int mutate_prog(const unsigned char *prog, int len, double rate,
                 unsigned char *result, Rng *rng);
int crossover_prog(const unsigned char *a, int alen, const unsigned char *b, int blen,
                    unsigned char *result, Rng *rng);
double fitness_fn(const VMResult *out, const int *target, int target_len, int plen,
                  bool exact);
/* exact: salida == target y parada limpia. Si no: la salida sólo debe empezar por target. */
bool is_solved(const VMResult *out, const int *target, int target_len, bool exact);

/* ¿Hay en el flujo de instrucciones un salto con desplazamiento negativo (un bucle)? */
bool has_backward_jump(const unsigned char *code, int n);

MetaResult metabiology(
    const int *target, int target_len,
    int pop_size, int generations, long eval_budget,
    double mutation_rate, double xover_prob, double elite_frac, double inject_frac,
    bool exact, Individual *initial_pop, int initial_pop_len,
    bool verbose, Rng *rng
);

void metabiology_free(MetaResult *mr);

#endif
