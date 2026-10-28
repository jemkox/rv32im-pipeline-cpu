/*
 * hdl_lint.c - small static checker for the Verilog / SystemVerilog in rtl/
 *
 * Usage: hdl_lint [-x errors warnings] <file.v>...
 *
 * It tokenizes each file (comments and strings removed) and checks things a
 * simulator would only catch late, or not at all:
 *
 *   errors
 *     - module/endmodule, begin/end and case/endcase that do not balance
 *     - "if (posedge x)" : an edge is not a condition, use "if (x)"
 *     - sized literals with a digit that is invalid for the base (4'b0105)
 *     - always_ff without an event control
 *     - blocking "=" in always_ff, nonblocking "<=" in always_comb
 *     - one signal driven by two always blocks, or by an assign and an always
 *   warnings
 *     - case statements without a default branch
 *     - sized literals whose value does not fit in the stated width
 *
 * It does not elaborate the design, so it cannot see port-connection or
 * width problems. Use "iverilog -Wall" for those; this tool complements it.
 *
 * With -x the tool instead succeeds only if the counts match exactly; this is
 * how the self-test (tools/lint_selftest/bad.v) is verified.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char kind;          /* 'i' identifier, 'n' number, 's' symbol */
    char text[72];
    int  line;
} Tok;

static Tok *toks;
static int  ntok, captok;
static int  errors, warnings;
static const char *cur_file;

static void report(const char *sev, int line, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#include <stdarg.h>
static void report(const char *sev, int line, const char *fmt, ...)
{
    va_list ap;
    printf("%s:%d: %s: ", cur_file, line, sev);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    if (sev[0] == 'e') errors++;
    else warnings++;
}

/* ----------------------------------------------------------------------- */
/* tokenizer                                                                */
/* ----------------------------------------------------------------------- */

static void push(char kind, const char *s, size_t n, int line)
{
    Tok *t;
    if (ntok == captok) {
        captok = captok ? captok * 2 : 4096;
        toks = realloc(toks, (size_t)captok * sizeof *toks);
        if (!toks) {
            fprintf(stderr, "out of memory\n");
            exit(2);
        }
    }
    t = &toks[ntok++];
    if (n > sizeof t->text - 1) n = sizeof t->text - 1;
    memcpy(t->text, s, n);
    t->text[n] = '\0';
    t->kind = kind;
    t->line = line;
}

static int is_id_start(int c) { return isalpha(c) || c == '_' || c == '$'; }
static int is_id_char(int c)  { return isalnum(c) || c == '_' || c == '$'; }
static int is_base(int c)     { return strchr("bBoOdDhH", c) != NULL && c != 0; }

static const char *sym3[] = {"===", "!==", ">>>", "<<<", NULL};
static const char *sym2[] = {"<=", ">=", "==", "!=", "<<", ">>", "&&", "||", "**",
                             "~&", "~|", "~^", "^~", "->", "+:", "-:", NULL};

static void tokenize(const char *s)
{
    int line = 1;
    size_t i = 0, n = strlen(s);

    ntok = 0;
    while (i < n) {
        char c = s[i];
        size_t j, k;

        if (c == '\n') { line++; i++; continue; }
        if (isspace((unsigned char)c)) { i++; continue; }

        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n && s[i] != '\n') i++;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) {
                if (s[i] == '\n') line++;
                i++;
            }
            i += 2;
            continue;
        }
        if (c == '"') {
            i++;
            while (i < n && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < n) i++;
                if (s[i] == '\n') line++;
                i++;
            }
            i++;
            continue;
        }
        if (c == '`') {                         /* compiler directive */
            i++;
            while (i < n && is_id_char((unsigned char)s[i])) i++;
            continue;
        }
        if (is_id_start((unsigned char)c)) {
            j = i;
            while (j < n && is_id_char((unsigned char)s[j])) j++;
            push('i', s + i, j - i, line);
            i = j;
            continue;
        }
        if (isdigit((unsigned char)c) ||
            (c == '\'' && i + 1 < n && (is_base((unsigned char)s[i + 1]) ||
                                       ((s[i + 1] == 's' || s[i + 1] == 'S') && i + 2 < n && is_base((unsigned char)s[i + 2]))))) {
            j = i;
            while (j < n && (isdigit((unsigned char)s[j]) || s[j] == '_')) j++;
            if (j < n && s[j] == '\'') {
                j++;
                if (j < n && (s[j] == 's' || s[j] == 'S')) j++;
                if (j < n && is_base((unsigned char)s[j])) {
                    j++;
                    while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '?')) j++;
                }
            }
            push('n', s + i, j - i, line);
            i = j;
            continue;
        }
        /* symbols */
        for (k = 0; sym3[k]; k++) {
            if (strncmp(s + i, sym3[k], 3) == 0) break;
        }
        if (sym3[k]) { push('s', s + i, 3, line); i += 3; continue; }
        for (k = 0; sym2[k]; k++) {
            if (strncmp(s + i, sym2[k], 2) == 0) break;
        }
        if (sym2[k]) { push('s', s + i, 2, line); i += 2; continue; }
        push('s', s + i, 1, line);
        i++;
    }
}

static int is(int i, const char *text)
{
    return i >= 0 && i < ntok && strcmp(toks[i].text, text) == 0;
}

/* ----------------------------------------------------------------------- */
/* literal check                                                            */
/* ----------------------------------------------------------------------- */

static void check_number(const Tok *t)
{
    const char *s = t->text;
    const char *q = strchr(s, '\'');
    const char *p;
    char        base, clean[72];
    unsigned long size = 0;
    int         has_xz = 0, nclean = 0;

    if (!q) return;
    if (q > s) size = strtoul(s, NULL, 10);
    p = q + 1;
    if (*p == 's' || *p == 'S') p++;
    if (!*p) return;
    base = (char)tolower((unsigned char)*p);
    if (!strchr("bodh", base)) return;
    p++;

    for (; *p; p++) {
        char c = (char)tolower((unsigned char)*p);
        int  ok;
        if (c == '_') continue;
        if (c == 'x' || c == 'z' || c == '?') {
            has_xz = 1;
            continue;
        }
        switch (base) {
        case 'b': ok = (c == '0' || c == '1'); break;
        case 'o': ok = (c >= '0' && c <= '7'); break;
        case 'd': ok = (c >= '0' && c <= '9'); break;
        default:  ok = isxdigit((unsigned char)c); break;
        }
        if (!ok) {
            report("error", t->line, "invalid digit '%c' in %s literal '%s'", *p, base == 'b' ? "binary" : base == 'o' ? "octal" : base == 'd' ? "decimal" : "hex", s);
            return;
        }
        clean[nclean++] = c;
    }
    clean[nclean] = '\0';

    if (!has_xz && size > 0 && size < 64 && nclean > 0) {
        int b = base == 'b' ? 2 : base == 'o' ? 8 : base == 'd' ? 10 : 16;
        unsigned long long v = strtoull(clean, NULL, b);
        if (v >> size) {
            report("warning", t->line, "literal '%s' does not fit in %lu bits", s, size);
        }
    }
}

/* ----------------------------------------------------------------------- */
/* driver tracking                                                          */
/* ----------------------------------------------------------------------- */

typedef struct {
    char name[72];
    int  block;         /* id of the assign statement or always block */
    int  is_assign;
    int  partial;       /* assigned through a bit/part/array select */
    int  line;
} Driver;

static Driver *drv;
static int     ndrv, capdrv;
static int     block_counter;

static void add_driver(const char *name, int block, int is_assign, int partial, int line)
{
    Driver *d;
    if (ndrv == capdrv) {
        capdrv = capdrv ? capdrv * 2 : 256;
        drv = realloc(drv, (size_t)capdrv * sizeof *drv);
        if (!drv) {
            fprintf(stderr, "out of memory\n");
            exit(2);
        }
    }
    d = &drv[ndrv++];
    strncpy(d->name, name, sizeof d->name - 1);
    d->name[sizeof d->name - 1] = '\0';
    d->block = block;
    d->is_assign = is_assign;
    d->partial = partial;
    d->line = line;
}

static void check_drivers(void)
{
    int i, j;
    for (i = 0; i < ndrv; i++) {
        int first = 1;
        if (drv[i].partial) continue;
        for (j = 0; j < i; j++) {
            if (!drv[j].partial && strcmp(drv[j].name, drv[i].name) == 0) first = 0;
        }
        if (!first) continue;               /* report each signal once */
        for (j = i + 1; j < ndrv; j++) {
            if (!drv[j].partial && strcmp(drv[j].name, drv[i].name) == 0 &&
                drv[j].block != drv[i].block) {
                report("error", drv[j].line,
                       "signal '%s' has multiple drivers (%s at line %d, %s at line %d)",
                       drv[i].name,
                       drv[i].is_assign ? "assign" : "always block", drv[i].line,
                       drv[j].is_assign ? "assign" : "always block", drv[j].line);
                break;
            }
        }
    }
    ndrv = 0;
}

static int is_type_kw(int i)
{
    static const char *kw[] = {"int", "integer", "logic", "reg", "bit", "byte", NULL};
    int k;
    if (i < 0 || i >= ntok) return 0;
    for (k = 0; kw[k]; k++) if (strcmp(toks[i].text, kw[k]) == 0) return 1;
    return 0;
}

/* Walk back from the token before an assignment operator to the target name. */
static int lhs_name(int k, char *out, int *partial)
{
    int i = k - 1;
    *partial = 0;
    while (is(i, "]")) {                    /* skip [..] selects */
        int depth = 0;
        *partial = 1;
        for (; i >= 0; i--) {
            if (is(i, "]")) depth++;
            else if (is(i, "[")) {
                depth--;
                if (depth == 0) { i--; break; }
            }
        }
    }
    if (i < 0 || toks[i].kind != 'i') return 0;
    strcpy(out, toks[i].text);
    return 1;
}

/* Check one always_ff / always_comb block starting at token i. */
static void scan_always(int i)
{
    int ff   = is(i, "always_ff");
    int comb = is(i, "always_comb");
    int j = i + 1, s, e, k;
    int pd = 0, bd = 0, assigned = 0;
    int block = ++block_counter;

    if (ff) {
        if (!is(j, "@")) {
            report("error", toks[i].line, "always_ff has no event control (@(posedge clk ...))");
        } else {
            j++;
            if (is(j, "(")) {
                int depth = 0;
                for (; j < ntok; j++) {
                    if (is(j, "(")) depth++;
                    else if (is(j, ")") && --depth == 0) { j++; break; }
                }
            }
        }
    }

    s = j;
    if (is(s, "begin")) {
        int depth = 0;
        for (e = s; e < ntok; e++) {
            if (is(e, "begin")) depth++;
            else if (is(e, "end") && --depth == 0) { e++; break; }
        }
    } else {
        for (e = s; e < ntok && !is(e, ";"); e++) {}
        e++;
        while (is(e, "else")) {
            for (; e < ntok && !is(e, ";"); e++) {}
            e++;
        }
    }
    if (e > ntok) e = ntok;

    for (k = s; k < e; k++) {
        if (is(k, "(")) pd++;
        else if (is(k, ")")) pd--;
        else if (is(k, "[")) bd++;
        else if (is(k, "]")) bd--;
        else if (is(k, ";") || is(k, "begin") || is(k, "end") || is(k, "else")) assigned = 0;
        else if ((is(k, "=") || is(k, "<=")) && pd == 0 && bd == 0 && !assigned) {
            char name[72];
            int  partial;
            int  blocking = is(k, "=");
            assigned = 1;
            if (blocking && is_type_kw(k - 2)) continue;       /* declaration with initialiser */
            if (!lhs_name(k, name, &partial)) continue;
            if (ff && blocking)
                report("error", toks[k].line, "blocking assignment to '%s' in always_ff (use <=)", name);
            if (comb && !blocking)
                report("error", toks[k].line, "nonblocking assignment to '%s' in always_comb (use =)", name);
            add_driver(name, block, 0, partial, toks[k].line);
        }
    }
}

/* ----------------------------------------------------------------------- */
/* per-file checks                                                          */
/* ----------------------------------------------------------------------- */

static void check_file(const char *src)
{
    struct { int line; int has_default; int guarded; } cases[64];
    int begins[256];
    int ncase = 0, nbegin = 0, depth_module = 0;
    int i;

    tokenize(src);
    ndrv = 0;

    for (i = 0; i < ntok; i++) {
        const Tok *t = &toks[i];

        if (t->kind == 'n') {
            check_number(t);
            continue;
        }
        if (t->kind != 'i') continue;

        if (strcmp(t->text, "module") == 0) {
            if (depth_module) report("error", t->line, "module inside module (missing endmodule)");
            depth_module = 1;
        } else if (strcmp(t->text, "endmodule") == 0) {
            if (!depth_module) report("error", t->line, "endmodule without module");
            depth_module = 0;
            check_drivers();
        } else if (strcmp(t->text, "begin") == 0) {
            if (nbegin < 256) begins[nbegin] = t->line;
            nbegin++;
        } else if (strcmp(t->text, "end") == 0) {
            if (nbegin == 0) report("error", t->line, "'end' without begin");
            else nbegin--;
        } else if (strcmp(t->text, "case") == 0 || strcmp(t->text, "casez") == 0 ||
                   strcmp(t->text, "casex") == 0) {
            if (ncase < 64) {
                cases[ncase].line = t->line;
                cases[ncase].has_default = 0;
                cases[ncase].guarded = is(i - 1, "unique") || is(i - 1, "priority");
            }
            ncase++;
        } else if (strcmp(t->text, "default") == 0) {
            if (ncase > 0 && ncase <= 64) cases[ncase - 1].has_default = 1;
        } else if (strcmp(t->text, "endcase") == 0) {
            if (ncase == 0) {
                report("error", t->line, "endcase without case");
            } else {
                ncase--;
                if (ncase < 64 && !cases[ncase].has_default && !cases[ncase].guarded)
                    report("warning", cases[ncase].line, "case statement has no default branch");
            }
        } else if (strcmp(t->text, "if") == 0 && is(i + 1, "(") &&
                   (is(i + 2, "posedge") || is(i + 2, "negedge"))) {
            report("error", t->line, "'if (%s ...)' is not a valid condition; test the signal itself", toks[i + 2].text);
        } else if (strcmp(t->text, "assign") == 0) {
            int j = i + 1;
            if (is(j, "#")) j += 2;
            if (j < ntok && toks[j].kind == 'i') {
                add_driver(toks[j].text, ++block_counter, 1, is(j + 1, "["), t->line);
            }
        } else if (strcmp(t->text, "always_ff") == 0 || strcmp(t->text, "always_comb") == 0) {
            scan_always(i);
        }
    }

    for (; nbegin > 0; nbegin--)
        report("error", nbegin <= 256 ? begins[nbegin - 1] : 0, "'begin' is never closed");
    for (; ncase > 0; ncase--)
        report("error", ncase <= 64 ? cases[ncase - 1].line : 0, "'case' is never closed");
    if (depth_module) report("error", toks[ntok - 1].line, "missing endmodule");
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long  n;
    char *buf;

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    n = (long)fread(buf, 1, (size_t)n, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    int i = 1, files = 0;
    int want_e = -1, want_w = -1;

    if (argc > 3 && strcmp(argv[1], "-x") == 0) {
        want_e = atoi(argv[2]);
        want_w = atoi(argv[3]);
        i = 4;
    }
    if (i >= argc) {
        fprintf(stderr, "usage: %s [-x errors warnings] <file.v>...\n", argv[0]);
        return 2;
    }
    for (; i < argc; i++) {
        char *src = read_file(argv[i]);
        if (!src) {
            fprintf(stderr, "cannot read %s\n", argv[i]);
            return 2;
        }
        cur_file = argv[i];
        check_file(src);
        free(src);
        files++;
    }
    printf("hdl_lint: %d file(s), %d error(s), %d warning(s)\n", files, errors, warnings);

    if (want_e >= 0) {
        if (errors != want_e || warnings != want_w) {
            printf("hdl_lint: SELF-TEST FAILED, expected %d error(s) and %d warning(s)\n", want_e, want_w);
            return 1;
        }
        return 0;
    }
    return errors ? 1 : 0;
}
