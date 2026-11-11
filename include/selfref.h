#ifndef SELFREF_H
#define SELFREF_H

#include <stdbool.h>
#include "vm.h"

/*
 * Censo exhaustivo de todos los programas de longitud L:
 *   - cómo terminan (parada limpia, buffer lleno, error, sin parar dentro del presupuesto)
 *   - cuántos son quines (salida == código) y quines inversos (salida == código al revés)
 *   - cuántos imprimen *algo* de su propio código (autorreferencia parcial)
 */

#define CENSUS_MAX_EXAMPLES 64

typedef struct {
    int L;
    long budget;
    long total;
    long by_status[5];          /* índice = VMStatus */
    long quines;
    long reverse_quines;
    long quines_without_read;   /* quines "a la Kleene": sin autoinspección */
    int n_quine_examples;
    unsigned char quine_examples[CENSUS_MAX_EXAMPLES][MAX_PROG_LEN];
    int n_rquine_examples;
    unsigned char rquine_examples[CENSUS_MAX_EXAMPLES][MAX_PROG_LEN];
    double wall_time;
} Census;

void census_length(int L, long budget, Census *c);

/*
 * Búsqueda perezosa de programas autodescriptivos de longitud n.
 *
 * En vez de enumerar los 16^n programas, se ejecuta un programa *parcial*:
 * sólo se ramifica (16 opciones) cuando la ejecución toca una posición aún
 * desconocida (al ejecutarla, leer su argumento o hacer READ sobre ella).
 * Cada OUT en la posición de salida j *fuerza* el símbolo code[map(j)]
 * sin ramificar. Para quines, la salida fuerza el programa entero, así que
 * el árbol es diminuto comparado con 16^n y el conteo es exacto.
 *
 *   SELF_IDENTITY : salida[j] == code[j]          (quine si m == n)
 *   SELF_REVERSE  : salida[j] == code[n-1-j]      (quine inverso si m == n)
 */
typedef enum { SELF_IDENTITY = 0, SELF_REVERSE = 1 } SelfMap;

typedef struct {
    int n, m;
    SelfMap map;
    long budget;
    double count;               /* número exacto de programas (double: puede ser 16^k) */
    double count_without_read;
    long nodes;                 /* nodos del árbol de búsqueda */
    int n_examples;
    unsigned char examples[CENSUS_MAX_EXAMPLES][MAX_PROG_LEN];
    long verify_failures;       /* debe ser 0: cada hallazgo se re-verifica con run_vm */
    double wall_time;
} SelfSearch;

void self_search(int n, int m, SelfMap map, long budget, SelfSearch *s);

/* ¿Contiene el programa una instrucción READ alcanzable sintácticamente? */
bool has_read_opcode(const unsigned char *code, int n);

#endif
