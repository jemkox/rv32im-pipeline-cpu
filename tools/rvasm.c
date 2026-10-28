/*
 * rvasm.c - two-pass assembler for the RV32I subset implemented by this CPU
 *
 * Usage: rvasm <input.s> <output.hex>
 *
 * Output is one 32-bit word per line in hex, which is what $readmemh reads.
 *
 * Supported
 *   R:       add sub sll slt sltu xor srl sra or and
 *   M:       mul mulh mulhsu mulhu div divu rem remu
 *   I:       addi slti sltiu xori ori andi slli srli srai
 *   memory:  lb lh lw lbu lhu rd, off(rs1)    sb sh sw rs2, off(rs1)
 *   branch:  beq bne blt bge bltu bgeu   (label or numeric offset)
 *   jump:    jal [rd,] label    jalr rd, rs1, imm    jalr rd, off(rs1)
 *   upper:   lui auipc
 *   pseudo:  nop li mv not neg j jr ret call beqz bnez bgt ble bgtu bleu
 *            seqz snez
 *   data:    .word value
 *   labels:  "name:" alone or before an instruction; comments start with
 *            '#' or "//".
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES  4096
#define MAX_LABELS 1024
#define MAX_WORDS  256
#define MAX_TOK    8

typedef struct {
    char     name[64];
    uint32_t addr;
} Label;

static char   *src_lines[MAX_LINES];
static int     n_src;
static Label   labels[MAX_LABELS];
static int     n_labels;
static uint32_t words[MAX_WORDS];
static int     n_words;
static int     errors;
static int     pass;
static int     cur_line;
static const char *src_name;

static void err(const char *fmt, ...)
{
    va_list ap;
    if (pass == 0) return;               /* report each problem once */
    errors++;
    fprintf(stderr, "%s:%d: error: ", src_name, cur_line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ----------------------------------------------------------------------- */
/* registers and numbers                                                    */
/* ----------------------------------------------------------------------- */

static const char *abi_names[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0",   "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6",   "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

static int parse_reg(const char *t)
{
    char *end;
    long  n;
    int   i;

    if (t[0] == 'x' && isdigit((unsigned char)t[1])) {
        n = strtol(t + 1, &end, 10);
        if (*end == '\0' && n >= 0 && n < 32) return (int)n;
    }
    for (i = 0; i < 32; i++) {
        if (strcmp(t, abi_names[i]) == 0) return i;
    }
    if (strcmp(t, "fp") == 0) return 8;
    return -1;
}

static int reg_or_err(const char *t)
{
    int r = parse_reg(t);
    if (r < 0) {
        err("bad register '%s'", t);
        return 0;
    }
    return r;
}

static int parse_num(const char *t, long long *out)
{
    char *end;
    long long v;

    if (*t == '\0') return 0;
    v = strtoll(t, &end, 0);
    if (*end != '\0') return 0;
    *out = v;
    return 1;
}

static long long num_or_err(const char *t)
{
    long long v = 0;
    if (!parse_num(t, &v)) err("bad number '%s'", t);
    return v;
}

/* ----------------------------------------------------------------------- */
/* labels                                                                   */
/* ----------------------------------------------------------------------- */

static void define_label(const char *name)
{
    int i;
    for (i = 0; i < n_labels; i++) {
        if (strcmp(labels[i].name, name) == 0) {
            if (pass == 0) {
                fprintf(stderr, "%s:%d: error: label '%s' defined twice\n",
                        src_name, cur_line, name);
                errors++;
            }
            return;
        }
    }
    if (n_labels >= MAX_LABELS) {
        fprintf(stderr, "too many labels\n");
        exit(1);
    }
    strncpy(labels[n_labels].name, name, sizeof labels[n_labels].name - 1);
    labels[n_labels].addr = (uint32_t)n_words * 4u;
    n_labels++;
}

static int find_label(const char *name, uint32_t *addr)
{
    int i;
    for (i = 0; i < n_labels; i++) {
        if (strcmp(labels[i].name, name) == 0) {
            *addr = labels[i].addr;
            return 1;
        }
    }
    return 0;
}

/* Target of a branch / jump: a label or a numeric pc-relative offset. */
static long long target_offset(const char *t)
{
    long long v;
    uint32_t  addr;
    uint32_t  pc = (uint32_t)n_words * 4u;

    if (parse_num(t, &v)) return v;
    if (find_label(t, &addr)) return (long long)addr - (long long)pc;
    if (pass == 1) err("undefined label '%s'", t);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* encoders                                                                 */
/* ----------------------------------------------------------------------- */

static void emit(uint32_t w)
{
    if (n_words >= MAX_WORDS) {
        if (pass == 1) err("program larger than %d words", MAX_WORDS);
        return;
    }
    words[n_words++] = w;
}

static uint32_t enc_r(uint32_t op, uint32_t f3, uint32_t f7, int rd, int rs1, int rs2)
{
    return (f7 << 25) | ((uint32_t)rs2 << 20) | ((uint32_t)rs1 << 15) |
           (f3 << 12) | ((uint32_t)rd << 7) | op;
}

static uint32_t enc_i(uint32_t op, uint32_t f3, int rd, int rs1, long long imm)
{
    if (imm < -2048 || imm > 2047) err("immediate %lld out of range (-2048..2047)", imm);
    return (((uint32_t)imm & 0xfffu) << 20) | ((uint32_t)rs1 << 15) |
           (f3 << 12) | ((uint32_t)rd << 7) | op;
}

static uint32_t enc_s(uint32_t f3, int rs1, int rs2, long long imm)
{
    uint32_t u = (uint32_t)imm;
    if (imm < -2048 || imm > 2047) err("offset %lld out of range (-2048..2047)", imm);
    return (((u >> 5) & 0x7fu) << 25) | ((uint32_t)rs2 << 20) | ((uint32_t)rs1 << 15) |
           (f3 << 12) | ((u & 0x1fu) << 7) | 0x23u;
}

static uint32_t enc_b(uint32_t f3, int rs1, int rs2, long long imm)
{
    uint32_t u = (uint32_t)imm;
    if (imm & 1) err("branch offset %lld is not even", imm);
    if (imm < -4096 || imm > 4094) err("branch offset %lld out of range", imm);
    return (((u >> 12) & 1u) << 31) | (((u >> 5) & 0x3fu) << 25) |
           ((uint32_t)rs2 << 20) | ((uint32_t)rs1 << 15) | (f3 << 12) |
           (((u >> 1) & 0xfu) << 8) | (((u >> 11) & 1u) << 7) | 0x63u;
}

static uint32_t enc_u(uint32_t op, int rd, long long imm)
{
    if (imm < -524288 || imm > 1048575) err("upper immediate %lld out of range", imm);
    return (((uint32_t)imm & 0xfffffu) << 12) | ((uint32_t)rd << 7) | op;
}

static uint32_t enc_j(int rd, long long imm)
{
    uint32_t u = (uint32_t)imm;
    if (imm & 1) err("jump offset %lld is not even", imm);
    if (imm < -1048576 || imm > 1048574) err("jump offset %lld out of range", imm);
    return (((u >> 20) & 1u) << 31) | (((u >> 1) & 0x3ffu) << 21) |
           (((u >> 11) & 1u) << 20) | (((u >> 12) & 0xffu) << 12) |
           ((uint32_t)rd << 7) | 0x6fu;
}

/* ----------------------------------------------------------------------- */
/* instruction tables                                                       */
/* ----------------------------------------------------------------------- */

typedef struct { const char *name; uint32_t f3, f7; } RType;
static const RType r_ops[] = {
    {"add", 0, 0x00}, {"sub", 0, 0x20}, {"sll", 1, 0x00}, {"slt", 2, 0x00},
    {"sltu", 3, 0x00}, {"xor", 4, 0x00}, {"srl", 5, 0x00}, {"sra", 5, 0x20},
    {"or", 6, 0x00}, {"and", 7, 0x00},
    {"mul", 0, 0x01}, {"mulh", 1, 0x01}, {"mulhsu", 2, 0x01}, {"mulhu", 3, 0x01},
    {"div", 4, 0x01}, {"divu", 5, 0x01}, {"rem", 6, 0x01}, {"remu", 7, 0x01},
    {NULL, 0, 0}
};

typedef struct { const char *name; uint32_t f3; } F3Op;
static const F3Op i_ops[] = {
    {"addi", 0}, {"slti", 2}, {"sltiu", 3}, {"xori", 4}, {"ori", 6}, {"andi", 7}, {NULL, 0}
};
static const F3Op l_ops[] = {
    {"lb", 0}, {"lh", 1}, {"lw", 2}, {"lbu", 4}, {"lhu", 5}, {NULL, 0}
};
static const F3Op s_ops[] = {
    {"sb", 0}, {"sh", 1}, {"sw", 2}, {NULL, 0}
};
static const F3Op b_ops[] = {
    {"beq", 0}, {"bne", 1}, {"blt", 4}, {"bge", 5}, {"bltu", 6}, {"bgeu", 7}, {NULL, 0}
};

static const RType *find_r(const char *n)
{
    int i;
    for (i = 0; r_ops[i].name; i++) if (strcmp(r_ops[i].name, n) == 0) return &r_ops[i];
    return NULL;
}

static const F3Op *find_f3(const F3Op *tab, const char *n)
{
    int i;
    for (i = 0; tab[i].name; i++) if (strcmp(tab[i].name, n) == 0) return &tab[i];
    return NULL;
}

/* "off(reg)" -> off, reg */
static void parse_mem(const char *t, long long *off, int *reg)
{
    char buf[64];
    char *lp, *rp;

    strncpy(buf, t, sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';
    lp = strchr(buf, '(');
    rp = strchr(buf, ')');
    if (!lp || !rp || rp < lp) {
        err("expected offset(register), got '%s'", t);
        *off = 0;
        *reg = 0;
        return;
    }
    *lp = '\0';
    *rp = '\0';
    *off = (lp == buf) ? 0 : num_or_err(buf);
    *reg = reg_or_err(lp + 1);
}

static void need(int n, int want, const char *mn)
{
    if (n != want) err("'%s' expects %d operand(s), got %d", mn, want, n);
}

/* ----------------------------------------------------------------------- */
/* one instruction                                                          */
/* ----------------------------------------------------------------------- */

static int imm_fits12(long long v) { return v >= -2048 && v <= 2047; }

static void assemble(char **t, int nt)
{
    const char *mn = t[0];
    int n = nt - 1;                 /* operand count */
    const RType *r;
    const F3Op  *f;
    long long    v, off;
    int          rd, rs1, rs2;

    if (strcmp(mn, ".word") == 0) {
        need(n, 1, mn);
        v = num_or_err(t[1]);
        emit((uint32_t)v);
        return;
    }

    /* ---- pseudo instructions ---- */
    if (strcmp(mn, "nop") == 0) { emit(enc_i(0x13, 0, 0, 0, 0)); return; }

    if (strcmp(mn, "li") == 0) {
        need(n, 2, mn);
        rd = reg_or_err(t[1]);
        v  = num_or_err(t[2]);
        if (v < -2147483648LL || v > 4294967295LL) err("li value out of 32-bit range");
        if (imm_fits12(v)) {
            emit(enc_i(0x13, 0, rd, 0, v));
        } else {
            int32_t  sv = (int32_t)(uint32_t)v;
            uint32_t hi = ((uint32_t)sv + 0x800u) >> 12;
            int32_t  lo = sv - (int32_t)(hi << 12);
            emit(enc_u(0x37, rd, (long long)hi));
            emit(enc_i(0x13, 0, rd, rd, lo));
        }
        return;
    }
    if (strcmp(mn, "mv") == 0)   { need(n, 2, mn); emit(enc_i(0x13, 0, reg_or_err(t[1]), reg_or_err(t[2]), 0)); return; }
    if (strcmp(mn, "not") == 0)  { need(n, 2, mn); emit(enc_i(0x13, 4, reg_or_err(t[1]), reg_or_err(t[2]), -1)); return; }
    if (strcmp(mn, "neg") == 0)  { need(n, 2, mn); emit(enc_r(0x33, 0, 0x20, reg_or_err(t[1]), 0, reg_or_err(t[2]))); return; }
    if (strcmp(mn, "seqz") == 0) { need(n, 2, mn); emit(enc_i(0x13, 3, reg_or_err(t[1]), reg_or_err(t[2]), 1)); return; }
    if (strcmp(mn, "snez") == 0) { need(n, 2, mn); emit(enc_r(0x33, 3, 0, reg_or_err(t[1]), 0, reg_or_err(t[2]))); return; }
    if (strcmp(mn, "j") == 0)    { need(n, 1, mn); emit(enc_j(0, target_offset(t[1]))); return; }
    if (strcmp(mn, "call") == 0) { need(n, 1, mn); emit(enc_j(1, target_offset(t[1]))); return; }
    if (strcmp(mn, "jr") == 0)   { need(n, 1, mn); emit(enc_i(0x67, 0, 0, reg_or_err(t[1]), 0)); return; }
    if (strcmp(mn, "ret") == 0)  { need(n, 0, mn); emit(enc_i(0x67, 0, 0, 1, 0)); return; }
    if (strcmp(mn, "beqz") == 0) { need(n, 2, mn); emit(enc_b(0, reg_or_err(t[1]), 0, target_offset(t[2]))); return; }
    if (strcmp(mn, "bnez") == 0) { need(n, 2, mn); emit(enc_b(1, reg_or_err(t[1]), 0, target_offset(t[2]))); return; }
    if (strcmp(mn, "bgt") == 0)  { need(n, 3, mn); emit(enc_b(4, reg_or_err(t[2]), reg_or_err(t[1]), target_offset(t[3]))); return; }
    if (strcmp(mn, "ble") == 0)  { need(n, 3, mn); emit(enc_b(5, reg_or_err(t[2]), reg_or_err(t[1]), target_offset(t[3]))); return; }
    if (strcmp(mn, "bgtu") == 0) { need(n, 3, mn); emit(enc_b(6, reg_or_err(t[2]), reg_or_err(t[1]), target_offset(t[3]))); return; }
    if (strcmp(mn, "bleu") == 0) { need(n, 3, mn); emit(enc_b(7, reg_or_err(t[2]), reg_or_err(t[1]), target_offset(t[3]))); return; }

    /* ---- real instructions ---- */
    if ((r = find_r(mn)) != NULL) {
        need(n, 3, mn);
        emit(enc_r(0x33, r->f3, r->f7, reg_or_err(t[1]), reg_or_err(t[2]), reg_or_err(t[3])));
        return;
    }
    if ((f = find_f3(i_ops, mn)) != NULL) {
        need(n, 3, mn);
        emit(enc_i(0x13, f->f3, reg_or_err(t[1]), reg_or_err(t[2]), num_or_err(t[3])));
        return;
    }
    if (strcmp(mn, "slli") == 0 || strcmp(mn, "srli") == 0 || strcmp(mn, "srai") == 0) {
        uint32_t f3 = (mn[1] == 'l' && mn[2] == 'l') ? 1u : 5u;
        uint32_t f7 = (strcmp(mn, "srai") == 0) ? 0x20u : 0u;
        need(n, 3, mn);
        rd  = reg_or_err(t[1]);
        rs1 = reg_or_err(t[2]);
        v   = num_or_err(t[3]);
        if (v < 0 || v > 31) err("shift amount %lld out of range (0..31)", v);
        emit(enc_r(0x13, f3, f7, rd, rs1, (int)(v & 31)));
        return;
    }
    if ((f = find_f3(l_ops, mn)) != NULL) {
        need(n, 2, mn);
        rd = reg_or_err(t[1]);
        parse_mem(t[2], &off, &rs1);
        emit(enc_i(0x03, f->f3, rd, rs1, off));
        return;
    }
    if ((f = find_f3(s_ops, mn)) != NULL) {
        need(n, 2, mn);
        rs2 = reg_or_err(t[1]);
        parse_mem(t[2], &off, &rs1);
        emit(enc_s(f->f3, rs1, rs2, off));
        return;
    }
    if ((f = find_f3(b_ops, mn)) != NULL) {
        need(n, 3, mn);
        emit(enc_b(f->f3, reg_or_err(t[1]), reg_or_err(t[2]), target_offset(t[3])));
        return;
    }
    if (strcmp(mn, "jal") == 0) {
        if (n == 1) {
            emit(enc_j(1, target_offset(t[1])));
        } else {
            need(n, 2, mn);
            emit(enc_j(reg_or_err(t[1]), target_offset(t[2])));
        }
        return;
    }
    if (strcmp(mn, "jalr") == 0) {
        if (n == 1) {
            emit(enc_i(0x67, 0, 1, reg_or_err(t[1]), 0));
        } else if (n == 2) {
            rd = reg_or_err(t[1]);
            parse_mem(t[2], &off, &rs1);
            emit(enc_i(0x67, 0, rd, rs1, off));
        } else {
            need(n, 3, mn);
            emit(enc_i(0x67, 0, reg_or_err(t[1]), reg_or_err(t[2]), num_or_err(t[3])));
        }
        return;
    }
    if (strcmp(mn, "lui") == 0 || strcmp(mn, "auipc") == 0) {
        need(n, 2, mn);
        emit(enc_u(mn[0] == 'l' ? 0x37u : 0x17u, reg_or_err(t[1]), num_or_err(t[2])));
        return;
    }

    err("unknown instruction '%s'", mn);
}

/* ----------------------------------------------------------------------- */
/* driver                                                                   */
/* ----------------------------------------------------------------------- */

static void process_line(const char *line)
{
    char  buf[512];
    char *tok[MAX_TOK + 1];
    int   nt = 0;
    char *p, *c;

    strncpy(buf, line, sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';

    c = strchr(buf, '#');
    if (c) *c = '\0';
    c = strstr(buf, "//");
    if (c) *c = '\0';

    /* split on whitespace and commas */
    p = strtok(buf, " \t\r\n,");
    while (p && nt < MAX_TOK) {
        tok[nt++] = p;
        p = strtok(NULL, " \t\r\n,");
    }
    if (p) err("too many operands");

    /* leading labels */
    while (nt > 0) {
        size_t len = strlen(tok[0]);
        if (len > 1 && tok[0][len - 1] == ':') {
            tok[0][len - 1] = '\0';
            define_label(tok[0]);
            memmove(tok, tok + 1, (size_t)(nt - 1) * sizeof tok[0]);
            nt--;
        } else {
            break;
        }
    }
    if (nt == 0) return;
    assemble(tok, nt);
}

int main(int argc, char **argv)
{
    FILE *in, *out;
    char  line[512];
    int   i;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <input.s> <output.hex>\n", argv[0]);
        return 2;
    }
    src_name = argv[1];
    in = fopen(argv[1], "r");
    if (!in) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    while (fgets(line, sizeof line, in)) {
        if (n_src >= MAX_LINES) {
            fprintf(stderr, "source too long\n");
            return 2;
        }
        src_lines[n_src] = strdup(line);
        n_src++;
    }
    fclose(in);

    for (pass = 0; pass < 2; pass++) {
        n_words = 0;
        for (i = 0; i < n_src; i++) {
            cur_line = i + 1;
            process_line(src_lines[i]);
        }
    }
    if (errors) {
        fprintf(stderr, "%d error(s)\n", errors);
        return 1;
    }

    out = fopen(argv[2], "w");
    if (!out) {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 2;
    }
    for (i = 0; i < n_words; i++) fprintf(out, "%08x\n", words[i]);
    fclose(out);
    printf("%s: %d words\n", argv[1], n_words);
    return 0;
}
