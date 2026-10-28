/*
 * rvgen.c - random test program generator
 *
 * Usage: rvgen <seed> <output.s> [instructions]
 *
 * Writes an assembly program of random ALU, load/store, branch and jump
 * instructions that always terminates. The program is run on the RTL and on
 * the C model, and the final states must match. Source registers are often
 * chosen to be the previous destination, which creates dense forwarding and
 * load-use hazards.
 *
 * Termination: branches and jumps only go forward, over 1..3 simple
 * instructions. x30 holds a fixed data base address and is never written, so
 * every load and store stays inside data memory.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t rng_state;

static uint32_t rnd(void)                    /* xorshift32 */
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int pick(int n) { return (int)(rnd() % (uint32_t)n); }

#define NREG 15                              /* x1..x15 are used */
static int last_rd = 1;
static int label_id;

static int src_reg(void)
{
    int r = pick(8);
    if (r == 0) return 0;                    /* x0 */
    if (r <= 3) return last_rd;              /* hazard with the previous result */
    return 1 + pick(NREG);
}

static int dst_reg(void)
{
    last_rd = 1 + pick(NREG);
    return last_rd;
}

static long rand_imm12(void)
{
    switch (pick(4)) {
    case 0:  return pick(8) - 4;
    case 1:  return pick(4096) - 2048;
    case 2:  return (pick(2) ? 2047 : -2048);
    default: return pick(64) - 32;
    }
}

/* one non-control instruction */
static void simple(FILE *f)
{
    static const char *rops[] = {"add", "sub", "sll", "slt", "sltu", "xor", "srl", "sra", "or", "and",
                                 "mul", "mulh", "mulhsu", "mulhu", "div", "divu", "rem", "remu"};
    static const char *lops[] = {"lw", "lb", "lh", "lbu", "lhu"};
    static const char *sops[2] = {"sb", "sh"};
    static const char *iops[] = {"addi", "slti", "sltiu", "xori", "ori", "andi"};
    static const char *shops[] = {"slli", "srli", "srai"};
    int k = pick(100);

    if (k < 38) {
        int rs1 = src_reg(), rs2 = src_reg();
        fprintf(f, "    %-6s x%d, x%d, x%d\n", rops[pick(18)], dst_reg(), rs1, rs2);
    } else if (k < 62) {
        int rs1 = src_reg();
        fprintf(f, "    %-5s x%d, x%d, %ld\n", iops[pick(6)], dst_reg(), rs1, rand_imm12());
    } else if (k < 70) {
        int rs1 = src_reg();
        fprintf(f, "    %-5s x%d, x%d, %d\n", shops[pick(3)], dst_reg(), rs1, pick(32));
    } else if (k < 74) {
        fprintf(f, "    lui   x%d, %d\n", dst_reg(), pick(1 << 20));
    } else if (k < 76) {
        fprintf(f, "    auipc x%d, %d\n", dst_reg(), pick(1 << 20));
    } else if (k < 86) {
        int which = pick(5);                                   /* lw lb lh lbu lhu */
        int off = (which == 0) ? 4 * pick(32) : (which == 2 || which == 4) ? 2 * pick(64) : pick(128);
        fprintf(f, "    %-5s x%d, %d(x30)\n", lops[which], dst_reg(), off);
    } else if (k < 88) {
        /* load / store with a funct3 that does not exist: must act as a NOP */
        if (pick(2)) {
            static const unsigned bad_l[] = {3, 6, 7};
            fprintf(f, "    .word 0x%08x\n",
                    (30u << 15) | (bad_l[pick(3)] << 12) | ((unsigned)(1 + pick(NREG)) << 7) | 0x03u);
        } else {
            fprintf(f, "    .word 0x%08x\n",
                    ((unsigned)src_reg() << 20) | (30u << 15) | ((unsigned)(3 + pick(5)) << 12) | 0x23u);
        }
    } else {
        int which = pick(3);                                   /* sb sh sw */
        int off = (which == 2) ? 4 * pick(32) : (which == 1) ? 2 * pick(64) : pick(128);
        int rs2 = src_reg();
        fprintf(f, "    %-5s x%d, %d(x30)\n", which == 2 ? "sw" : sops[which], rs2, off);
    }
}

int main(int argc, char **argv)
{
    FILE *f;
    int   n = 90;
    int   i, k;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <seed> <output.s> [instructions]\n", argv[0]);
        return 2;
    }
    rng_state = (uint32_t)strtoul(argv[1], NULL, 0) * 2654435761u + 0x9e3779b9u;
    if (rng_state == 0) rng_state = 1;
    for (i = 0; i < 8; i++) rnd();
    if (argc > 3) n = atoi(argv[3]);

    f = fopen(argv[2], "w");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 2;
    }
    fprintf(f, "# random program, seed %s\n", argv[1]);
    fprintf(f, "    li    x30, 512\n");
    for (i = 1; i <= NREG; i++) fprintf(f, "    li    x%d, %d\n", i, (int)rnd());

    for (i = 0; i < n; i++) {
        int kind = pick(100);
        if (kind < 78) {
            simple(f);
        } else if (kind < 94) {
            static const char *bops[] = {"beq", "bne", "blt", "bge", "bltu", "bgeu"};
            int skip = 1 + pick(3);
            int id = label_id++;
            int rs1 = src_reg(), rs2 = src_reg();
            fprintf(f, "    %-5s x%d, x%d, L%d\n", bops[pick(6)], rs1, rs2, id);
            for (k = 0; k < skip; k++) simple(f);
            fprintf(f, "L%d:\n", id);
        } else {
            int skip = 1 + pick(3);
            int id = label_id++;
            fprintf(f, "    jal   x%d, L%d\n", pick(2) ? 0 : dst_reg(), id);
            for (k = 0; k < skip; k++) simple(f);
            fprintf(f, "L%d:\n", id);
        }
    }
    fprintf(f, "halt:\n    j     halt\n");
    fclose(f);
    return 0;
}
