#include "selfref.h"
#include "levin.h"
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#ifdef _OPENMP
#include <omp.h>
#endif

bool has_read_opcode(const unsigned char *code, int n) {
    /* Conservador: un salto puede caer en mitad de un argumento y ejecutarlo
     * como opcode, así que basta con que el símbolo READ aparezca en
     * cualquier posición. Si no aparece, seguro que el programa no se lee. */
    for (int j = 0; j < n; j++) if ((code[j] & 15) == OP_READ) return true;
    return false;
}

static int cmp_prog(const void *a, const void *b) {
    return memcmp(a, b, MAX_PROG_LEN);
}

static void decode_index(uint64_t idx, int L, unsigned char *tup) {
    for (int i = L - 1; i >= 0; i--) {
        tup[i] = (unsigned char)(idx & 15);
        idx >>= SYMBOL_BITS;
    }
}

void census_length(int L, long budget, Census *c) {
    memset(c, 0, sizeof(*c));
    c->L = L;
    c->budget = budget;
    uint64_t total = (uint64_t)1 << (SYMBOL_BITS * L);
    c->total = (long)total;

    long st0 = 0, st1 = 0, st2 = 0, st3 = 0, st4 = 0;
    long q = 0, rq = 0, qnr = 0;

#ifdef _OPENMP
    double t0 = omp_get_wtime();
#else
    clock_t t0 = clock();
#endif

    #pragma omp parallel for schedule(dynamic, 4096) reduction(+:st0,st1,st2,st3,st4,q,rq,qnr)
    for (uint64_t idx = 0; idx < total; idx++) {
        unsigned char tup[MAX_PROG_LEN];
        decode_index(idx, L, tup);
        long steps;
        VMResult out = run_vm(tup, L, budget, &steps);
        switch (out.status) {
            case VM_HALT: st0++; break;
            case VM_OUT_FULL: st1++; break;
            case VM_CRASH: st2++; break;
            case VM_TIMEOUT: st3++; break;
            default: st4++; break;
        }
        if (out.status != VM_HALT || out.len != L) continue;

        bool is_q = true, is_rq = true;
        for (int i = 0; i < L; i++) {
            if (out.values[i] != tup[i]) is_q = false;
            if (out.values[i] != tup[L - 1 - i]) is_rq = false;
        }
        if (is_q) {
            q++;
            if (!has_read_opcode(tup, L)) qnr++;
            #pragma omp critical(quine_ex)
            {
                if (c->n_quine_examples < CENSUS_MAX_EXAMPLES)
                    memcpy(c->quine_examples[c->n_quine_examples++], tup, (size_t)L);
            }
        }
        if (is_rq) {
            rq++;
            #pragma omp critical(rquine_ex)
            {
                if (c->n_rquine_examples < CENSUS_MAX_EXAMPLES)
                    memcpy(c->rquine_examples[c->n_rquine_examples++], tup, (size_t)L);
            }
        }
    }

    /* orden determinista de los ejemplos (OpenMP los guarda en orden arbitrario) */
    qsort(c->quine_examples, (size_t)c->n_quine_examples, MAX_PROG_LEN, cmp_prog);
    qsort(c->rquine_examples, (size_t)c->n_rquine_examples, MAX_PROG_LEN, cmp_prog);

    c->by_status[VM_HALT] = st0;
    c->by_status[VM_OUT_FULL] = st1;
    c->by_status[VM_CRASH] = st2;
    c->by_status[VM_TIMEOUT] = st3;
    c->by_status[VM_MISMATCH] = st4;
    c->quines = q;
    c->reverse_quines = rq;
    c->quines_without_read = qnr;
#ifdef _OPENMP
    c->wall_time = omp_get_wtime() - t0;
#else
    c->wall_time = (double)(clock() - t0) / CLOCKS_PER_SEC;
#endif
}

/* ------------------------------------------------------------------ */
/* Búsqueda perezosa con símbolos desconocidos                         */
/* ------------------------------------------------------------------ */

enum { PR_FAIL = 0, PR_FOUND = 1, PR_NEED = 2 };

static inline int map_pos(SelfMap map, int n, int j) {
    return map == SELF_IDENTITY ? j : n - 1 - j;
}

/*
 * Ejecuta un programa parcialmente conocido. Debe reproducir *exactamente*
 * la semántica de vm_core (mismo orden de comprobaciones y mismo conteo de
 * pasos). Devuelve PR_NEED con *need = posición desconocida cuando la
 * ejecución depende de ella. Las asignaciones forzadas por OUT se escriben
 * en code/known (el llamador trabaja sobre una copia).
 */
static int run_partial(unsigned char *code, unsigned char *known, int n, int m,
                       SelfMap map, long budget, int *need) {
    int stack[MAX_STK];
    int sp = 0, pc = 0, outn = 0;
    long steps = 0;

#define NEED(p) do { *need = (p); return PR_NEED; } while (0)

    while (steps < budget) {
        if (pc == n) return outn == m ? PR_FOUND : PR_FAIL;
        if (!known[pc]) NEED(pc);
        int op = code[pc];
        steps++;

        switch (op) {
            case OP_HALT:
                return outn == m ? PR_FOUND : PR_FAIL;

            case OP_OUT: {
                if (sp == 0) return PR_FAIL;
                int v = stack[--sp];
                if (outn >= m) return PR_FAIL;          /* sobra salida */
                if (v < 0 || v > 15) return PR_FAIL;    /* no es un símbolo */
                int p = map_pos(map, n, outn);
                if (known[p]) {
                    if (code[p] != v) return PR_FAIL;
                } else {
                    code[p] = (unsigned char)v;          /* forzado, sin ramificar */
                    known[p] = 1;
                }
                outn++;
                if (outn >= MAX_OUT) return PR_FAIL;     /* VM_OUT_FULL no es parada limpia */
                pc += 1;
                break;
            }

            case OP_DUP:
                if (sp == 0 || sp >= MAX_STK) return PR_FAIL;
                stack[sp] = stack[sp - 1];
                sp++;
                pc += 1;
                break;

            case OP_SWAP:
                if (sp < 2) return PR_FAIL;
                { int t = stack[sp - 1]; stack[sp - 1] = stack[sp - 2]; stack[sp - 2] = t; }
                pc += 1;
                break;

            case OP_ADD: case OP_SUB: case OP_MUL: {
                if (sp < 2) return PR_FAIL;
                long b = stack[--sp];
                long a = stack[--sp];
                long v = op == OP_ADD ? a + b : op == OP_SUB ? a - b : a * b;
                if ((v < 0 ? -v : v) > INT_CAP) return PR_FAIL;
                stack[sp++] = (int)v;
                pc += 1;
                break;
            }

            case OP_INC: case OP_DEC: {
                if (sp == 0) return PR_FAIL;
                long v = (long)stack[sp - 1] + (op == OP_INC ? 1 : -1);
                if ((v < 0 ? -v : v) > INT_CAP) return PR_FAIL;
                stack[sp - 1] = (int)v;
                pc += 1;
                break;
            }

            case OP_PUSH0: case OP_PUSH1:
                if (sp >= MAX_STK) return PR_FAIL;
                stack[sp++] = op == OP_PUSH1;
                pc += 1;
                break;

            case OP_READ: {
                if (sp == 0) return PR_FAIL;
                int i = stack[sp - 1];
                if (i < 0 || i >= n) return PR_FAIL;
                if (!known[i]) NEED(i);
                stack[sp - 1] = code[i];
                pc += 1;
                break;
            }

            case OP_LIT:
                if (pc + 1 >= n || sp >= MAX_STK) return PR_FAIL;
                if (!known[pc + 1]) NEED(pc + 1);
                stack[sp++] = code[pc + 1];
                pc += 2;
                break;

            case OP_JMP: case OP_JZ: case OP_JNZ: {
                if (pc + 1 >= n) return PR_FAIL;
                bool take = true;
                if (op != OP_JMP) {
                    if (sp == 0) return PR_FAIL;
                    int cond = stack[--sp];
                    take = (op == OP_JZ) ? (cond == 0) : (cond != 0);
                }
                if (take) {
                    if (!known[pc + 1]) NEED(pc + 1);
                    pc = pc + arg_signed(code[pc + 1]);
                } else {
                    pc = pc + 2;
                }
                if (pc < 0 || pc > n) return PR_FAIL;
                break;
            }

            default:
                return PR_FAIL;
        }
    }
#undef NEED
    if (pc == n) return outn == m ? PR_FOUND : PR_FAIL;
    return PR_FAIL;  /* sin parar dentro del presupuesto */
}

static bool verify_self(const unsigned char *code, int n, int m, SelfMap map, long budget) {
    long steps;
    VMResult out = run_vm(code, n, budget, &steps);
    if (out.status != VM_HALT || out.len != m) return false;
    for (int j = 0; j < m; j++)
        if (out.values[j] != code[map_pos(map, n, j)]) return false;
    return true;
}

static void lazy_rec(SelfSearch *s, const unsigned char *code_in, const unsigned char *known_in) {
    unsigned char code[MAX_PROG_LEN], known[MAX_PROG_LEN];
    memcpy(code, code_in, (size_t)s->n);
    memcpy(known, known_in, (size_t)s->n);
    s->nodes++;

    int need = -1;
    int r = run_partial(code, known, s->n, s->m, s->map, s->budget, &need);

    if (r == PR_FOUND) {
        /* Las posiciones aún desconocidas no influyen: cada una multiplica por 16. */
        int unknown = 0;
        for (int i = 0; i < s->n; i++) if (!known[i]) { unknown++; code[i] = 0; }
        double mult = 1.0;
        for (int i = 0; i < unknown; i++) mult *= 16.0;
        if (!verify_self(code, s->n, s->m, s->map, s->budget)) { s->verify_failures++; return; }
        s->count += mult;
        if (unknown == 0 && !has_read_opcode(code, s->n)) s->count_without_read += 1.0;
        if (s->n_examples < CENSUS_MAX_EXAMPLES)
            memcpy(s->examples[s->n_examples++], code, (size_t)s->n);
        return;
    }
    if (r == PR_FAIL) return;

    for (int v = 0; v < N_OPS; v++) {
        code[need] = (unsigned char)v;
        known[need] = 1;
        lazy_rec(s, code, known);
    }
}

void self_search(int n, int m, SelfMap map, long budget, SelfSearch *s) {
    memset(s, 0, sizeof(*s));
    s->n = n; s->m = m; s->map = map; s->budget = budget;
#ifdef _OPENMP
    double t0 = omp_get_wtime();
#else
    clock_t t0 = clock();
#endif
    /* La ejecución siempre necesita primero code[0]: se reparte esa
     * primera ramificación (16 subárboles) entre hilos. */
    s->nodes = 1;
    #pragma omp parallel for schedule(dynamic, 1)
    for (int v = 0; v < N_OPS; v++) {
        SelfSearch *loc = calloc(1, sizeof(SelfSearch));
        loc->n = n; loc->m = m; loc->map = map; loc->budget = budget;
        unsigned char code[MAX_PROG_LEN] = {0}, known[MAX_PROG_LEN] = {0};
        code[0] = (unsigned char)v;
        known[0] = 1;
        lazy_rec(loc, code, known);
        #pragma omp critical(self_merge)
        {
            s->count += loc->count;
            s->count_without_read += loc->count_without_read;
            s->nodes += loc->nodes;
            s->verify_failures += loc->verify_failures;
            for (int i = 0; i < loc->n_examples && s->n_examples < CENSUS_MAX_EXAMPLES; i++)
                memcpy(s->examples[s->n_examples++], loc->examples[i], MAX_PROG_LEN);
        }
        free(loc);
    }
    qsort(s->examples, (size_t)s->n_examples, MAX_PROG_LEN, cmp_prog);
#ifdef _OPENMP
    s->wall_time = omp_get_wtime() - t0;
#else
    s->wall_time = (double)(clock() - t0) / CLOCKS_PER_SEC;
#endif
}
