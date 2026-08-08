#ifndef VM_H
#define VM_H

#include <stdbool.h>

#define N_OPS 16
#define MAX_STK 64
#define MAX_OUT 32
#define INT_CAP 10000
#define MAX_PROG_LEN 64

extern const char *OPCODE_NAMES[N_OPS];

typedef struct {
    int values[MAX_OUT];
    int len;
    bool ok;
} VMResult;

bool op_has_arg(int op);
int signed_offset(unsigned char v);
VMResult run_vm(const unsigned char *code, int n, long budget, long *steps_used);
void hex_repr(const unsigned char *code, int n, char *out, int out_size);
void disassemble(const unsigned char *code, int n);

#endif
