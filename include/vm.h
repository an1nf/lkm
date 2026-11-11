#ifndef VM_H
#define VM_H

#include <stdbool.h>

/*
 * Máquina virtual de pila con alfabeto de 16 símbolos (nibbles).
 *
 * Cada posición del programa es un símbolo 0..15 (4 bits). Los opcodes con
 * argumento (LIT, JMP, JZ, JNZ) consumen el símbolo siguiente:
 *   - LIT n      apila n (0..15)
 *   - JMP/JZ/JNZ desplazamiento con signo -8..+7, relativo a la dirección
 *                de la propia instrucción de salto (destino = pc + off).
 *                Así existen saltos hacia atrás, bucles y no-terminación.
 *
 * READ (autoinspección, Laing 1977): desapila i y apila code[i].
 * Un programa puede leer su propia descripción: es la pieza que permite
 * quines y replicadores dentro de la máquina.
 */

#define N_OPS 16
#define SYMBOL_BITS 4
#define MAX_STK 64
#define MAX_OUT 32
#define INT_CAP 10000
#define MAX_PROG_LEN 64

enum {
    OP_HALT = 0, OP_OUT, OP_DUP, OP_SWAP,
    OP_ADD, OP_SUB, OP_MUL, OP_INC,
    OP_DEC, OP_PUSH0, OP_PUSH1, OP_READ,
    OP_LIT, OP_JMP, OP_JZ, OP_JNZ
};

typedef enum {
    VM_HALT = 0,     /* HALT o pc == n: parada limpia */
    VM_OUT_FULL,     /* se llenó el buffer de salida (MAX_OUT valores) */
    VM_CRASH,        /* error: pila vacía/llena, desborde, salto o READ fuera de rango */
    VM_TIMEOUT,      /* se agotó el presupuesto de pasos: no sabemos si para */
    VM_MISMATCH      /* sólo run_vm_expect: la salida dejó de coincidir con lo esperado */
} VMStatus;

extern const char *OPCODE_NAMES[N_OPS];
extern const char *VM_STATUS_NAMES[5];

typedef struct {
    int values[MAX_OUT];
    int len;
    bool ok;          /* status == VM_HALT || status == VM_OUT_FULL */
    VMStatus status;
} VMResult;

bool op_has_arg(int op);
int arg_signed(unsigned char v);

/* Ejecuta el programa con un presupuesto de pasos. */
VMResult run_vm(const unsigned char *code, int n, long budget, long *steps_used);

/*
 * Igual que run_vm pero aborta (VM_MISMATCH) en cuanto la salida se aparta
 * de expect[0..expect_len). Sirve para buscar quines rápido: la mayoría de
 * candidatos se descarta en el primer OUT.
 */
VMResult run_vm_expect(const unsigned char *code, int n, long budget, long *steps_used,
                       const int *expect, int expect_len);

void hex_repr(const unsigned char *code, int n, char *out, int out_size);
void disassemble(const unsigned char *code, int n);

#endif
