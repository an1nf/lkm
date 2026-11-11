/*
 * Pruebas: la búsqueda perezosa debe contar exactamente lo mismo que la
 * fuerza bruta sobre los 16^n programas, para salidas parciales (m < n)
 * donde los conteos no son triviales. También comprueba el quine manual.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "vm.h"
#include "selfref.h"

static long brute(int n, int m, int map, long budget) {
    long cnt = 0;
    uint64_t tot = 1ULL << (SYMBOL_BITS * n);
    #pragma omp parallel for reduction(+:cnt) schedule(dynamic, 4096)
    for (uint64_t idx = 0; idx < tot; idx++) {
        unsigned char t[MAX_PROG_LEN];
        uint64_t x = idx;
        for (int i = n - 1; i >= 0; i--) { t[i] = x & 15; x >>= 4; }
        long s;
        VMResult o = run_vm(t, n, budget, &s);
        if (o.status != VM_HALT || o.len != m) continue;
        int ok = 1;
        for (int j = 0; j < m; j++) {
            int p = map ? n - 1 - j : j;
            if (o.values[j] != t[p]) { ok = 0; break; }
        }
        cnt += ok;
    }
    return cnt;
}

int main(void) {
    int fails = 0, cases = 0;
    for (int n = 1; n <= 6; n++)
        for (int m = 0; m <= n && m <= 4; m++)
            for (int map = 0; map < 2; map++) {
                long b = brute(n, m, map, 64);
                SelfSearch *s = malloc(sizeof *s);
                self_search(n, m, (SelfMap)map, 64, s);
                cases++;
                if ((double)b != s->count || s->verify_failures) {
                    fails++;
                    printf("FAIL n=%d m=%d map=%d brute=%ld lazy=%.0f\n", n, m, map, b, s->count);
                }
                free(s);
            }

    unsigned char hand[] = {9, 8, 7, 2, 11, 2, 1, 15, 11, 0};
    long st;
    VMResult o = run_vm(hand, 10, 1000, &st);
    int quine_ok = o.status == VM_HALT && o.len == 10;
    for (int i = 0; quine_ok && i < 10; i++) quine_ok = o.values[i] == hand[i];
    if (!quine_ok) { fails++; printf("FAIL hand-made quine\n"); }

    printf("%d/%d lazy-vs-brute cases OK, hand quine %s\n", cases - (fails - !quine_ok), cases,
           quine_ok ? "OK" : "FAIL");
    return fails ? 1 : 0;
}
