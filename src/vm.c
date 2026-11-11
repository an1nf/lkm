#include "vm.h"
#include <stdio.h>
#include <string.h>

const char *OPCODE_NAMES[N_OPS] = {
    "HALT", "OUT",   "DUP",   "SWAP",
    "ADD",  "SUB",   "MUL",   "INC",
    "DEC",  "PUSH0", "PUSH1", "READ",
    "LIT",  "JMP",   "JZ",    "JNZ",
};

const char *VM_STATUS_NAMES[5] = {"halt", "out_full", "crash", "timeout", "mismatch"};

bool op_has_arg(int op) {
    return op == OP_LIT || op == OP_JMP || op == OP_JZ || op == OP_JNZ;
}

/* Nibble con signo en complemento a dos: 0..7 -> 0..7, 8..15 -> -8..-1. */
int arg_signed(unsigned char v) {
    int x = v & 15;
    return x >= 8 ? x - 16 : x;
}

static inline long iabs_long(long v) {
    return v < 0 ? -v : v;
}

static inline VMResult vm_core(const unsigned char *code, int n, long budget,
                               long *steps_used, const int *expect, int expect_len) {
    VMResult res;
    res.len = 0;
    res.ok = false;
    res.status = VM_TIMEOUT;

    int stack[MAX_STK];
    int sp = 0;
    int pc = 0;
    long steps = 0;

#define STOP(s) do { res.status = (s); goto stop; } while (0)

    while (steps < budget) {
        if (pc == n) STOP(VM_HALT);

        int op = code[pc] & 15;
        steps++;

        switch (op) {
            case OP_HALT:
                STOP(VM_HALT);

            case OP_OUT: {
                if (sp == 0) STOP(VM_CRASH);
                int v = stack[--sp];
                if (expect) {
                    if (res.len >= expect_len || expect[res.len] != v) STOP(VM_MISMATCH);
                }
                res.values[res.len++] = v;
                if (res.len >= MAX_OUT) STOP(VM_OUT_FULL);
                pc += 1;
                break;
            }

            case OP_DUP:
                if (sp == 0 || sp >= MAX_STK) STOP(VM_CRASH);
                stack[sp] = stack[sp - 1];
                sp++;
                pc += 1;
                break;

            case OP_SWAP:
                if (sp < 2) STOP(VM_CRASH);
                { int t = stack[sp - 1]; stack[sp - 1] = stack[sp - 2]; stack[sp - 2] = t; }
                pc += 1;
                break;

            case OP_ADD: case OP_SUB: case OP_MUL: {
                if (sp < 2) STOP(VM_CRASH);
                long b = stack[--sp];
                long a = stack[--sp];
                long v = op == OP_ADD ? a + b : op == OP_SUB ? a - b : a * b;
                if (iabs_long(v) > INT_CAP) STOP(VM_CRASH);
                stack[sp++] = (int)v;
                pc += 1;
                break;
            }

            case OP_INC: case OP_DEC: {
                if (sp == 0) STOP(VM_CRASH);
                long v = (long)stack[sp - 1] + (op == OP_INC ? 1 : -1);
                if (iabs_long(v) > INT_CAP) STOP(VM_CRASH);
                stack[sp - 1] = (int)v;
                pc += 1;
                break;
            }

            case OP_PUSH0: case OP_PUSH1:
                if (sp >= MAX_STK) STOP(VM_CRASH);
                stack[sp++] = op == OP_PUSH1;
                pc += 1;
                break;

            case OP_READ: {
                if (sp == 0) STOP(VM_CRASH);
                int i = stack[sp - 1];
                if (i < 0 || i >= n) STOP(VM_CRASH);
                stack[sp - 1] = code[i] & 15;
                pc += 1;
                break;
            }

            case OP_LIT:
                if (pc + 1 >= n || sp >= MAX_STK) STOP(VM_CRASH);
                stack[sp++] = code[pc + 1] & 15;
                pc += 2;
                break;

            case OP_JMP: case OP_JZ: case OP_JNZ: {
                if (pc + 1 >= n) STOP(VM_CRASH);
                bool take = true;
                if (op != OP_JMP) {
                    if (sp == 0) STOP(VM_CRASH);
                    int cond = stack[--sp];
                    take = (op == OP_JZ) ? (cond == 0) : (cond != 0);
                }
                pc = take ? pc + arg_signed(code[pc + 1]) : pc + 2;
                if (pc < 0 || pc > n) STOP(VM_CRASH);
                break;
            }

            default:
                STOP(VM_CRASH);
        }
    }
    /* presupuesto agotado sin parar */
    if (pc == n) res.status = VM_HALT;  /* llegó justo al final en el último paso */
#undef STOP

stop:
    res.ok = (res.status == VM_HALT || res.status == VM_OUT_FULL);
    *steps_used = steps;
    return res;
}

VMResult run_vm(const unsigned char *code, int n, long budget, long *steps_used) {
    return vm_core(code, n, budget, steps_used, NULL, 0);
}

VMResult run_vm_expect(const unsigned char *code, int n, long budget, long *steps_used,
                       const int *expect, int expect_len) {
    return vm_core(code, n, budget, steps_used, expect, expect_len);
}

void hex_repr(const unsigned char *code, int n, char *out, int out_size) {
    int pos = 0;
    out[0] = '\0';
    for (int i = 0; i < n && pos < out_size - 1; i++) {
        int written = snprintf(out + pos, (size_t)(out_size - pos), "%x", code[i] & 15);
        if (written < 0 || written >= out_size - pos) break;
        pos += written;
    }
    out[pos] = '\0';
}

void disassemble(const unsigned char *code, int n) {
    int i = 0;
    while (i < n) {
        int op = code[i] & 15;
        const char *name = OPCODE_NAMES[op];
        if (op_has_arg(op) && i + 1 < n) {
            unsigned char raw = code[i + 1] & 15;
            if (op == OP_LIT) {
                printf("    %02d  %-5s %2d    ; push %d\n", i, name, raw, raw);
            } else {
                int off = arg_signed(raw);
                printf("    %02d  %-5s %2d    ; offset %+d -> pc=%d\n", i, name, raw, off, i + off);
            }
            i += 2;
        } else {
            printf("    %02d  %s\n", i, name);
            i += 1;
        }
    }
}
