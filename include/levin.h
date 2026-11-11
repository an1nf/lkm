#ifndef LEVIN_H
#define LEVIN_H

#include <stdbool.h>
#include "vm.h"

/*
 * Búsqueda universal de Levin (1973) sobre programas de L símbolos de 4 bits.
 *
 * En la fase con presupuesto total T = 2^k, cada programa p de longitud L
 * recibe floor(T * 2^-|p|) = floor(T / 16^L) pasos, con |p| = 4L bits.
 * Como hay 16^L programas de longitud L, cada longitud consume ~T pasos por
 * fase. Un programa de L símbolos que resuelve el problema en t pasos se
 * encuentra cuando T >= 16^L * t, es decir, en tiempo O(2^|p| * t): la
 * cota de Levin.
 *
 * Los programas que ya pararon (o fallaron) en una fase anterior sin
 * satisfacer el predicado se recuerdan en un bitmap y no se re-ejecutan.
 */

#define LEVIN_MAX_LEN 8         /* 16^8 = 4.3e9: límite práctico de enumeración */
#define LEVIN_BITMAP_MAX_LEN 7  /* bitmap de 16^7 bits = 32 MB */

/*
 * Predicado: ejecuta `code` con `budget` pasos y decide si resuelve la tarea.
 * Debe devolver en *out el resultado de la ejecución (para saber si paró).
 */
typedef bool (*ProgPred)(const unsigned char *code, int n, long budget,
                         long *steps_used, VMResult *out, void *ctx);

typedef struct {
    unsigned char program[MAX_PROG_LEN];
    int prog_len;
    bool found;
    VMResult output;
    long steps;          /* pasos de VM realmente ejecutados */
    long programs;       /* ejecuciones de programas (con repeticiones entre fases) */
    long phases;
    double wall_time;
} LResult;

typedef struct {
    int K_upper;         /* en símbolos; -1 si no se encontró */
    int K_bits;          /* 4 * K_upper */
    double ratio;
    unsigned char program[MAX_PROG_LEN];
    int prog_len;
    bool has_program;
    long steps;
} KResult;

long ipow(long base, int exp);
bool next_tuple(unsigned char *tup, int L, int base);
bool output_matches_target(const VMResult *out, const int *target, int target_len);

LResult levin_search_pred(ProgPred pred, void *ctx, int max_len,
                          long total_budget, bool verbose);

/* Búsqueda de un programa cuya salida empiece por target. */
LResult levin_search(const int *target, int target_len, int max_len,
                     long total_budget, bool verbose);

KResult kolmogorov_upper(const int *data, int data_len, int max_len, long total_budget);

/* Predicados de autorreferencia */
bool pred_quine(const unsigned char *code, int n, long budget,
                long *steps_used, VMResult *out, void *ctx);
bool pred_reverse_quine(const unsigned char *code, int n, long budget,
                        long *steps_used, VMResult *out, void *ctx);

#endif
