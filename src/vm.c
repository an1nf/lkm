#include "vm.h"
#include <stdio.h>
#include <string.h>

const char *OPCODE_NAMES[N_OPS] = {
    "HALT", "OUT",   "DUP",   "SWAP",
    "ADD",  "SUB",   "MUL",   "INC",
    "DEC",  "PUSH0", "PUSH1", "PUSH2",
    "LIT",  "JMP",   "JZ",    "JNZ",
};

bool op_has_arg(int op) {
    return op == 12 || op == 13 || op == 14 || op == 15;
}

int signed_offset(unsigned char v) {
    return v >= 128 ? (int)v - 256 : (int)v;
}

static inline long iabs_long(long v) {
    return v < 0 ? -v : v;
}

VMResult run_vm(const unsigned char *code, int n, long budget, long *steps_used) {
    VMResult res = {.len = 0, .ok = false};

    int stack[MAX_STK];
    int sp = 0;
    int pc = 0;
    long steps = 0;
    bool halted_clean = false;

    while (steps < budget) {
        if (pc >= n) {
            halted_clean = true;
            goto stop;
        }

        int op = code[pc];
        steps++;

        switch (op) {
            case 0:
                halted_clean = true;
                goto stop;

            case 1:
                if (sp == 0) goto stop;
                if (res.len < MAX_OUT) {
                    res.values[res.len++] = stack[--sp];
                } else {
                    sp--;
                }
                if (res.len >= MAX_OUT) { halted_clean = true; goto stop; }
                pc += 1;
                break;

            case 2:
                if (sp == 0 || sp >= MAX_STK) goto stop;
                stack[sp] = stack[sp - 1];
                sp++;
                pc += 1;
                break;

            case 3:
                if (sp < 2) goto stop;
                { int t = stack[sp - 1]; stack[sp - 1] = stack[sp - 2]; stack[sp - 2] = t; }
                pc += 1;
                break;

            case 4: {
                if (sp < 2) goto stop;
                int b = stack[--sp];
                int a = stack[--sp];
                long v = (long)a + (long)b;
                if (iabs_long(v) > INT_CAP) goto stop;
                stack[sp++] = (int)v;
                pc += 1;
                break;
            }

            case 5: {
                if (sp < 2) goto stop;
                int b = stack[--sp];
                int a = stack[--sp];
                long v = (long)a - (long)b;
                if (iabs_long(v) > INT_CAP) goto stop;
                stack[sp++] = (int)v;
                pc += 1;
                break;
            }

            case 6: {
                if (sp < 2) goto stop;
                int b = stack[--sp];
                int a = stack[--sp];
                long v = (long)a * (long)b;
                if (iabs_long(v) > INT_CAP) goto stop;
                stack[sp++] = (int)v;
                pc += 1;
                break;
            }

            case 7:
                if (sp == 0) goto stop;
                stack[sp - 1] += 1;
                pc += 1;
                break;

            case 8:
                if (sp == 0) goto stop;
                stack[sp - 1] -= 1;
                pc += 1;
                break;

            case 9:
                if (sp >= MAX_STK) goto stop;
                stack[sp++] = 0;
                pc += 1;
                break;

            case 10:
                if (sp >= MAX_STK) goto stop;
                stack[sp++] = 1;
                pc += 1;
                break;

            case 11:
                if (sp >= MAX_STK) goto stop;
                stack[sp++] = 2;
                pc += 1;
                break;

            case 12:
                if (pc + 1 >= n || sp >= MAX_STK) goto stop;
                stack[sp++] = code[pc + 1];
                pc += 2;
                break;

            case 13: {
                if (pc + 1 >= n) goto stop;
                int off = signed_offset(code[pc + 1]);
                pc = pc + 2 + off;
                if (pc < 0 || pc > n) goto stop;
                break;
            }

            case 14: {
                if (pc + 1 >= n || sp == 0) goto stop;
                int cond = stack[--sp];
                int off = signed_offset(code[pc + 1]);
                pc = (cond == 0) ? (pc + 2 + off) : (pc + 2);
                if (pc < 0 || pc > n) goto stop;
                break;
            }

            case 15: {
                if (pc + 1 >= n || sp == 0) goto stop;
                int cond = stack[--sp];
                int off = signed_offset(code[pc + 1]);
                pc = (cond != 0) ? (pc + 2 + off) : (pc + 2);
                if (pc < 0 || pc > n) goto stop;
                break;
            }

            default:
                goto stop;
        }
    }

stop:
    res.ok = halted_clean;
    *steps_used = steps;
    return res;
}

void hex_repr(const unsigned char *code, int n, char *out, int out_size) {
    int pos = 0;
    for (int i = 0; i < n && pos < out_size - 1; i++) {
        int written = snprintf(out + pos, out_size - pos, "%02x%s",
                                code[i], (i != n - 1) ? " " : "");
        if (written < 0 || written >= out_size - pos) break;
        pos += written;
    }
    out[pos] = '\0';
}

void disassemble(const unsigned char *code, int n) {
    int i = 0;
    while (i < n) {
        int op = code[i];
        const char *name = (op < N_OPS) ? OPCODE_NAMES[op] : "???";
        if (op_has_arg(op) && i + 1 < n) {
            unsigned char raw = code[i + 1];
            int off = signed_offset(raw);
            if (op == 12) {
                printf("    %02d  %-6s  %3d   ; push %d\n", i, name, raw, raw);
            } else {
                printf("    %02d  %-6s  %3d   ; offset %+d  -> pc=%d\n",
                       i, name, raw, off, i + 2 + off);
            }
            i += 2;
        } else {
            printf("    %02d  %s\n", i, name);
            i += 1;
        }
    }
}
