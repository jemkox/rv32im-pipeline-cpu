/*
 * rviss.c - golden instruction-set simulator for the RV32I subset in this CPU
 *
 * Usage: rviss <prog.hex> [-o dump.txt] [-c rtl_dump.txt] [-e name=value]...
 *
 *   -o  write the final register / memory state to a file
 *   -c  compare the final state against a dump produced by the RTL testbench;
 *       exit status 1 if anything differs
 *   -e  assert a value in the simulator's own final state, e.g. -e x5=55 or
 *       -e m4=0x10. This checks the model against hand-computed results.
 *
 * The model is purely architectural: one instruction per step, no pipeline.
 * It mirrors the RTL's memory map: 256 words of instruction memory and 256
 * words of data memory, both word addressed by address bits [9:2]. Byte
 * accesses use address bits [1:0], half-word accesses bit [1]. Opcodes the
 * CPU does not implement, and load / store funct3 values that do not exist,
 * behave as NOPs. Supports RV32I (without fence / system) and RV32M. A program
 * ends when an instruction jumps to itself (e.g. "halt: j halt").
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NWORDS 256
#define MAX_STEPS 5000000UL

static uint32_t imem[NWORDS];
static uint32_t dmem[NWORDS];
static uint32_t reg[32];
static uint32_t pc;
static unsigned long retired;

static int32_t sext(uint32_t v, int bits)
{
    uint32_t m = 1u << (bits - 1);
    return (int32_t)((v ^ m) - m);
}

static int load_hex(const char *path)
{
    FILE *f = fopen(path, "r");
    char  line[128];
    int   n = 0;
    int   i;

    if (!f) return -1;
    for (i = 0; i < NWORDS; i++) imem[i] = 0x00000013u;   /* NOP */
    while (fgets(line, sizeof line, f)) {
        char *end;
        unsigned long v;
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '/' || line[0] == '#') continue;
        v = strtoul(line, &end, 16);
        if (end == line) continue;
        if (n >= NWORDS) {
            fclose(f);
            return -2;
        }
        imem[n++] = (uint32_t)v;
    }
    fclose(f);
    return n;
}

/* Execute one instruction. Returns 1 if it jumped to itself (halt). */
static int step(void)
{
    uint32_t ins    = imem[(pc >> 2) & (NWORDS - 1)];
    uint32_t op     = ins & 0x7f;
    uint32_t rd     = (ins >> 7) & 31;
    uint32_t f3     = (ins >> 12) & 7;
    uint32_t rs1    = (ins >> 15) & 31;
    uint32_t rs2    = (ins >> 20) & 31;
    uint32_t f7     = ins >> 25;
    uint32_t a      = reg[rs1];
    uint32_t b      = reg[rs2];
    uint32_t next   = pc + 4;
    uint32_t result = 0;
    int      wr     = 0;

    int32_t imm_i = sext(ins >> 20, 12);
    int32_t imm_s = sext(((ins >> 25) << 5) | ((ins >> 7) & 0x1f), 12);
    int32_t imm_b = sext(((ins >> 31) << 12) | (((ins >> 7) & 1) << 11) |
                         (((ins >> 25) & 0x3f) << 5) | (((ins >> 8) & 0xf) << 1), 13);
    uint32_t imm_u = ins & 0xfffff000u;
    int32_t imm_j = sext(((ins >> 31) << 20) | (((ins >> 12) & 0xff) << 12) |
                         (((ins >> 20) & 1) << 11) | (((ins >> 21) & 0x3ff) << 1), 21);

    switch (op) {
    case 0x33:                                     /* R-type */
        wr = 1;
        if (f7 == 0x01) {                          /* RV32M */
            int64_t sa = (int32_t)a;
            int64_t sb = (int32_t)b;
            int     ovf = (a == 0x80000000u && b == 0xffffffffu);
            switch (f3) {
            case 0: result = (uint32_t)((uint64_t)a * (uint64_t)b); break;
            case 1: result = (uint32_t)((uint64_t)(sa * sb) >> 32); break;
            case 2: result = (uint32_t)((uint64_t)(sa * (int64_t)b) >> 32); break;
            case 3: result = (uint32_t)(((uint64_t)a * (uint64_t)b) >> 32); break;
            case 4: result = (b == 0) ? 0xffffffffu : ovf ? a : (uint32_t)((int32_t)a / (int32_t)b); break;
            case 5: result = (b == 0) ? 0xffffffffu : a / b; break;
            case 6: result = (b == 0) ? a : ovf ? 0u : (uint32_t)((int32_t)a % (int32_t)b); break;
            default: result = (b == 0) ? a : a % b; break;
            }
            break;
        }
        switch (f3) {
        case 0: result = (f7 & 0x20) ? a - b : a + b; break;
        case 1: result = a << (b & 31); break;
        case 2: result = ((int32_t)a < (int32_t)b); break;
        case 3: result = (a < b); break;
        case 4: result = a ^ b; break;
        case 5: result = (f7 & 0x20) ? (uint32_t)((int32_t)a >> (b & 31)) : a >> (b & 31); break;
        case 6: result = a | b; break;
        default: result = a & b; break;
        }
        break;
    case 0x13:                                     /* OP-IMM */
        wr = 1;
        switch (f3) {
        case 0: result = a + (uint32_t)imm_i; break;
        case 1: result = a << (rs2 & 31); break;
        case 2: result = ((int32_t)a < imm_i); break;
        case 3: result = (a < (uint32_t)imm_i); break;
        case 4: result = a ^ (uint32_t)imm_i; break;
        case 5: result = (f7 & 0x20) ? (uint32_t)((int32_t)a >> (rs2 & 31)) : a >> (rs2 & 31); break;
        case 6: result = a | (uint32_t)imm_i; break;
        default: result = a & (uint32_t)imm_i; break;
        }
        break;
    case 0x03: {                                   /* loads */
        uint32_t addr = a + (uint32_t)imm_i;
        uint32_t w    = dmem[(addr >> 2) & (NWORDS - 1)];
        uint32_t bv   = (w >> (8 * (addr & 3))) & 0xffu;
        uint32_t hv   = (w >> (16 * ((addr >> 1) & 1))) & 0xffffu;
        switch (f3) {
        case 0: result = (uint32_t)sext(bv, 8);  wr = 1; break;   /* lb  */
        case 1: result = (uint32_t)sext(hv, 16); wr = 1; break;   /* lh  */
        case 2: result = w;                      wr = 1; break;   /* lw  */
        case 4: result = bv;                     wr = 1; break;   /* lbu */
        case 5: result = hv;                     wr = 1; break;   /* lhu */
        default: break;                                           /* NOP */
        }
        break;
    }
    case 0x23: {                                   /* stores */
        uint32_t  addr = a + (uint32_t)imm_s;
        uint32_t *p    = &dmem[(addr >> 2) & (NWORDS - 1)];
        unsigned  sh;
        switch (f3) {
        case 0:  sh = 8 * (addr & 3);
                 *p = (*p & ~(0xffu << sh)) | ((b & 0xffu) << sh); break;       /* sb */
        case 1:  sh = 16 * ((addr >> 1) & 1);
                 *p = (*p & ~(0xffffu << sh)) | ((b & 0xffffu) << sh); break;   /* sh */
        case 2:  *p = b; break;                                                 /* sw */
        default: break;                                                         /* NOP */
        }
        break;
    }
    case 0x63: {                                   /* branches */
        int take = 0;
        switch (f3) {
        case 0: take = (a == b); break;
        case 1: take = (a != b); break;
        case 4: take = ((int32_t)a < (int32_t)b); break;
        case 5: take = ((int32_t)a >= (int32_t)b); break;
        case 6: take = (a < b); break;
        case 7: take = (a >= b); break;
        default: break;
        }
        if (take) next = pc + (uint32_t)imm_b;
        break;
    }
    case 0x6f:                                     /* JAL */
        wr = 1;
        result = pc + 4;
        next = pc + (uint32_t)imm_j;
        break;
    case 0x67:                                     /* JALR */
        wr = 1;
        result = pc + 4;
        next = (a + (uint32_t)imm_i) & ~1u;
        break;
    case 0x37:                                     /* LUI */
        wr = 1;
        result = imm_u;
        break;
    case 0x17:                                     /* AUIPC */
        wr = 1;
        result = pc + imm_u;
        break;
    default:                                       /* not implemented: NOP */
        break;
    }

    if (wr && rd != 0) reg[rd] = result;
    reg[0] = 0;
    retired++;
    if (next == pc) return 1;
    pc = next;
    return 0;
}

/* final state as text lines, identical in format to the RTL testbench dump */
static void state_line(char *buf, size_t n, int idx)
{
    if (idx < 32) snprintf(buf, n, "x%d %08x", idx, reg[idx]);
    else          snprintf(buf, n, "m%d %08x", idx - 32, dmem[idx - 32]);
}

static int write_dump(const char *path)
{
    FILE *f = fopen(path, "w");
    char  line[64];
    int   i;

    if (!f) return -1;
    for (i = 0; i < 32 + NWORDS; i++) {
        state_line(line, sizeof line, i);
        fprintf(f, "%s\n", line);
    }
    fclose(f);
    return 0;
}

static int compare_dump(const char *path)
{
    FILE *f = fopen(path, "r");
    char  got[128], want[64];
    int   i, bad = 0;

    if (!f) {
        fprintf(stderr, "cannot open RTL dump %s\n", path);
        return -1;
    }
    for (i = 0; i < 32 + NWORDS; i++) {
        size_t len;
        state_line(want, sizeof want, i);
        if (!fgets(got, sizeof got, f)) {
            printf("  RTL dump ended early at line %d\n", i + 1);
            bad++;
            break;
        }
        len = strlen(got);
        while (len > 0 && (got[len - 1] == '\n' || got[len - 1] == '\r')) got[--len] = '\0';
        if (strcmp(got, want) != 0) {
            if (bad < 20) printf("  MISMATCH  model: %s   rtl: %s\n", want, got);
            bad++;
        }
    }
    fclose(f);
    return bad;
}

/* -e name=value: x<n> or m<n> */
static int check_expect(const char *spec)
{
    char     name[32];
    char    *eq = strchr(spec, '=');
    char    *end;
    unsigned idx;
    uint32_t want, got;

    if (!eq || eq == spec || (size_t)(eq - spec) >= sizeof name) {
        fprintf(stderr, "bad -e argument '%s'\n", spec);
        return 1;
    }
    memcpy(name, spec, (size_t)(eq - spec));
    name[eq - spec] = '\0';
    want = (uint32_t)strtoul(eq + 1, &end, 0);
    if (*end != '\0') {
        fprintf(stderr, "bad value in '%s'\n", spec);
        return 1;
    }
    idx = (unsigned)strtoul(name + 1, &end, 10);
    if ((name[0] != 'x' && name[0] != 'm') || *end != '\0' ||
        (name[0] == 'x' && idx >= 32) || (name[0] == 'm' && idx >= NWORDS)) {
        fprintf(stderr, "bad location in '%s'\n", spec);
        return 1;
    }
    got = (name[0] == 'x') ? reg[idx] : dmem[idx];
    if (got != want) {
        printf("  EXPECT FAILED  %s: model has %08x, expected %08x\n", name, got, want);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *out_path = NULL, *cmp_path = NULL;
    const char *expects[64];
    int         n_expect = 0;
    int         halted = 0;
    int         n, i, status = 0;
    unsigned long steps;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <prog.hex> [-o dump] [-c rtl_dump] [-e x5=55]...\n", argv[0]);
        return 2;
    }
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)      out_path = argv[++i];
        else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) cmp_path = argv[++i];
        else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc && n_expect < 64) expects[n_expect++] = argv[++i];
        else {
            fprintf(stderr, "bad argument '%s'\n", argv[i]);
            return 2;
        }
    }

    n = load_hex(argv[1]);
    if (n < 0) {
        fprintf(stderr, "cannot load %s\n", argv[1]);
        return 2;
    }

    for (steps = 0; steps < MAX_STEPS; steps++) {
        if (step()) {
            halted = 1;
            break;
        }
    }
    if (!halted) {
        printf("  model did not halt after %lu steps\n", MAX_STEPS);
        return 1;
    }
    printf("model: %d words, %lu instructions executed\n", n, retired);

    if (out_path && write_dump(out_path) != 0) {
        fprintf(stderr, "cannot write %s\n", out_path);
        return 2;
    }
    for (i = 0; i < n_expect; i++) status |= check_expect(expects[i]);
    if (cmp_path) {
        int bad = compare_dump(cmp_path);
        if (bad < 0) return 2;
        if (bad > 0) {
            printf("  %d state difference(s) between model and RTL\n", bad);
            status = 1;
        }
    }
    return status;
}
