#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <string.h>
#include <assert.h>

#define MEM_SIZE    0x100000
#define ENTRY_POINT 0x4000
#define HALT_CODE   0xffffffff

uint32_t reg[32];
uint32_t mem[MEM_SIZE];
uint32_t pc;
uint32_t prog_size;
long long inst_cnt;

enum IOMode { RAW, HEX, INT, FLOAT };
enum IOMode read_mode = INT, write_mode = INT;
int read_pos, write_pos;
int read_buf[4], write_buf[4];

char infile[128];
int show_stat = 0;

void init_env()
{
    reg[30] = MEM_SIZE;
    reg[31] = MEM_SIZE;
    pc = ENTRY_POINT;
    prog_size = 0;
    inst_cnt = 0;
    read_pos = 4;
    write_pos = 0;
}

void print_env()
{
    fprintf(stderr, "<executed instruction count>: %lld\n", inst_cnt);
    fprintf(stderr, "<register>\n");
    for (int i = 0; i < 16; ++i)
        fprintf(stderr, "  r%-2d: %11d (0x%08x) / r%-2d: %11d (0x%08x)\n",
                i, reg[i], reg[i], i + 16, reg[i + 16], reg[i + 16]);
    fprintf(stderr, "<program counter>: 0x%08x\n", pc);
    fprintf(stderr, "<current instruction>: 0x%08x\n", mem[pc >> 2]);
}

void error(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    print_env();
    va_end(ap);
    exit(1);
}

uint32_t bitint(float x)
{
    union { uint32_t i; float f; } u;
    u.f = x;
    return u.i;
}

float bitfloat(uint32_t x)
{
    union { uint32_t i; float f; } u;
    u.i = x;
    return u.f;
}

uint32_t read()
{
    int x;
    float f;
    switch (read_mode) {
        case RAW:
            x = getchar();
            if (x == EOF) error("read error");
            return x;
        case HEX:
            if (scanf("%x", &x) < 1) error("read error");
            if (x < 0 || 255 < x) error("read: invalid input");
            return x;
        default:
            if (read_pos == 4) {
                if (read_mode == INT) {
                    if (scanf("%i", &x) < 1) error("read error");
                } else {
                    if (scanf("%f", &f) < 1) error("read error");
                    x = bitint(f);
                }
                for (int i = 3; i >= 0; --i)
                    read_buf[i] = x & 255, x >>= 8;
                read_pos = 0;
            }
            return read_buf[read_pos++];
    }
}

void write(uint32_t x)
{
    switch (write_mode) {
        case RAW:
            putchar(x & 255);
            return;
        case HEX:
            printf("%02x\n", x & 255);
            return;
        default:
            write_buf[write_pos++] = x & 255;
            if (write_pos == 4) {
                x = 0;
                for (int i = 0; i < 4; ++i)
                    x <<= 8, x += write_buf[i];
                if (write_mode == INT)
                    printf("%d\n", x);
                else
                    printf("%f\n", bitfloat(x));
                write_pos = 0;
            }
            return;
    }
}

uint32_t alu(int tag, int ra, int rb, int lit)
{
    switch (tag) {
        case  0: return reg[ra] + reg[rb] + lit;
        case  1: return reg[ra] - reg[rb] - lit;
        case  2: return reg[ra] << (reg[rb] + lit);
        case  3: return reg[ra] >> (reg[rb] + lit);
        case  4: return (int32_t)reg[ra] >> (reg[rb] + lit);
        case  5: return reg[ra] & reg[rb] & lit;
        case  6: return reg[ra] | reg[rb] | lit;
        case  7: return reg[ra] ^ reg[rb] ^ lit;
        case 24: return reg[ra] != reg[rb] + lit;
        case 25: return reg[ra] == reg[rb] + lit;
        case 26: return reg[ra] <  reg[rb] + lit;
        case 27: return reg[ra] <= reg[rb] + lit;
        case 28: return bitfloat(reg[ra]) != bitfloat(reg[rb]);
        case 29: return bitfloat(reg[ra]) == bitfloat(reg[rb]);
        case 30: return bitfloat(reg[ra]) <  bitfloat(reg[rb]);
        case 31: return bitfloat(reg[ra]) <= bitfloat(reg[rb]);
        default: assert(0);
    }
}

uint32_t fpu(int tag, int ra, int rb)
{
    switch (tag) {
        case 0:  return bitint(bitfloat(reg[ra]) + bitfloat(reg[rb]));
        case 1:  return bitint(bitfloat(reg[ra]) - bitfloat(reg[rb]));
        case 2:  return bitint(bitfloat(reg[ra]) * bitfloat(reg[rb]));
        case 3:  return bitint(bitfloat(reg[ra]) / bitfloat(reg[rb]));
        case 4:  return bitint(1.0 / bitfloat(reg[ra]));
        case 5:  return bitint(sqrtf(bitfloat(reg[ra])));
        case 6:  return (uint32_t)roundf(bitfloat(reg[ra]));
        case 7:  return bitint((float)reg[ra]);
        case 8:  return bitint(floorf(bitfloat(reg[ra])));
        default: assert(0);
    }
}

uint32_t sign(uint32_t x, int mode)
{
    switch (mode) {
        case 1:  return x ^ 0x80000000;
        case 2:  return x & 0x7fffffff;
        case 3:  return x | 0x80000000;
        default: return x;
    }
}

void store(int rx, int ra, int disp)
{
    int addr = reg[ra] + (disp << 2);
    if (addr & 3)
        error("store: invalid address: %08x", addr);
    if (addr == 0x300c)
        write(reg[rx]);
    else
        mem[addr >> 2] = reg[rx];
}

void load(int rx, int ra, int disp)
{
    int addr = reg[ra] + (disp << 2);
    if (addr & 3)
        error("load: invalid address: %08x", addr);
    switch (addr) {
        case 0x3000: reg[rx] = 1; return;
        case 0x3004: reg[rx] = read(); return;
        case 0x3008: reg[rx] = 1; return;
        default:     reg[rx] = mem[addr >> 2]; return;
    }
}

void exec_alu(uint32_t inst)
{
    int rx = (inst >> 23) & 31;
    int ra = (inst >> 18) & 31;
    int rb = (inst >> 13) & 31;
    int lit = (inst >> 5) & 255;
    int tag = inst & 31;
    if (lit >= 128) lit -= 256;
    reg[rx] = alu(tag, ra, rb, lit);
}

void exec_fpu(uint32_t inst)
{
    int rx = (inst >> 23) & 31;
    int ra = (inst >> 18) & 31;
    int rb = (inst >> 13) & 31;
    int mode = (inst >> 5) & 3;
    int tag = inst & 31;
    reg[rx] = sign(fpu(tag, ra, rb), mode);
}

void exec_other(uint32_t inst)
{
    int opcode = inst >> 28;
    int rx = (inst >> 23) & 31;
    int ra = (inst >> 18) & 31;
    int disp = inst & 0xffff;
    if (disp >= 0x8000) disp -= 0x10000;
    switch (opcode) {
        case  2: reg[rx] = disp; return;
        case  3: reg[rx] = (disp << 16) | (reg[ra] & 0xffff); return;
        case  6: store(rx, ra, disp); return;
        case  8: load(rx, ra, disp); return;
        case 11: reg[rx] = pc + 4; pc += disp << 2; return;
        case 12: pc = reg[rx] - 4; return;
        case 13: if (reg[rx] != reg[ra]) pc += disp << 2; return;
        case 15: if (reg[rx] == reg[ra]) pc += disp << 2; return;
    }
}

void exec(uint32_t inst)
{
    int opcode = inst >> 28;
    switch (opcode) {
        case 0:  return exec_alu(inst);
        case 1:  return exec_fpu(inst);
        default: return exec_other(inst);
    }
}

void load_file()
{
    int inst;
    FILE *fp = fopen(infile, "r");
    if (fp == NULL) { perror(infile); exit(1); }
    while (1) {
        inst = fgetc(fp);
        if (inst == EOF) return;
        for (int j = 1; j < 4; ++j)
            inst <<= 8, inst += fgetc(fp);
        mem[(ENTRY_POINT + prog_size) >> 2] = inst;
        prog_size += 4;
    }
    fclose(fp);
}

void runsim()
{
    init_env();
    load_file();
    while (1) {
        if (pc & 3 || pc >= ENTRY_POINT + prog_size)
            error("invalid program counter");
        if (mem[pc >> 2] == HALT_CODE) break;
        exec(mem[pc >> 2]);
        pc += 4;
        ++inst_cnt;
    }
}

void print_help(char *prog)
{
    printf("usage: %s [options] file\n", prog);
    printf("options:\n");
    printf("  -read <mode>    set read mode\n");
    printf("  -write <mode>   set write mode\n");
    printf("  -stat           show statistics\n");
    printf("mode:\n");
    printf("  raw, hex, int, float (default: int)\n");
    exit(1);
}

enum IOMode get_mode(char *mode, char *prog)
{
    if (strcmp(mode, "raw") == 0) return RAW;
    if (strcmp(mode, "hex") == 0) return HEX;
    if (strcmp(mode, "int") == 0) return INT;
    if (strcmp(mode, "float") == 0) return FLOAT;
    print_help(prog);
    return RAW; // suppress warning
}

void parse_cmd(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-read") == 0) {
            if (i == argc - 1) print_help(argv[0]);
            read_mode = get_mode(argv[++i], argv[0]);
        } else if (strcmp(argv[i], "-write") == 0) {
            if (i == argc - 1) print_help(argv[0]);
            write_mode = get_mode(argv[++i], argv[0]);
        } else if (strcmp(argv[i], "-stat") == 0) {
            show_stat = 1;
        } else {
            if (infile[0] != '\0') print_help(argv[0]);
            strcpy(infile, argv[i]);
        }
    }
}

int main(int argc, char *argv[])
{
    parse_cmd(argc, argv);
    if (infile[0] == '\0') print_help(argv[0]);
    runsim();
    if (show_stat) print_env();
    return 0;
}

