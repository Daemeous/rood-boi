/* The coverage lane's value probes (pc/tools/zig_diff.py --coverage): the
 * original C body is compiled at -O0 with --coverage (gcov branches, and
 * conditions on GCC 14+), -fsanitize-coverage=trace-cmp (every comparison's
 * operands) and -fsanitize=thread (every load's address: this file is the
 * runtime, not libtsan). Per instruction site it keeps what branch coverage
 * cannot see: whether a comparison's operands were ever equal (a `<` that
 * never met `==` cannot tell a port's `<=` apart), the range and sign of
 * what was tested, and the range of every value read. main.zig records the
 * arguments of each call (zdiff_cov_arg) and dumps everything at the end
 * (zdiff_cov_dump) to the file named by ZDIFF_COV_OUT, one record per line:
 *
 *   cmp PC SIZE CONST COUNT EQ SLT SGT ULT UGT AMIN AMAX BMIN BMAX ZERO NEG NZPOS NZNEG GTPOS GTNEG GTMIN DISTINCT V0 V1 ...
 *   read PC SIZE COUNT ADDR SAME SMIN SMAX UMIN UMAX ZERO NEG SMAXHIT UMAXHIT DISTINCT V0 V1 ...
 *   arg INDEX KIND BITS COUNT SMIN SMAX UMIN UMAX ZERO NEG SMAXHIT UMAXHIT DISTINCT V0 V1 ...
 *
 * (signed values for SMIN/SMAX/AMIN..., unsigned for UMIN/UMAX; DISTINCT is
 * the number of distinct values seen, capped at 9, followed by up to 8 of
 * them; GTPOS/GTNEG count the values above the other side (unsigned) that
 * were non-negative/negative as signed (a test `x > K` only ever true with
 * the sign bit set cannot tell a port's `x < 0` apart), GTMIN the smallest
 * of them (unsigned: all ones when only -1 was seen, as a 1-bit field
 * gives). zig_diff.py keeps only the sites inside the function's own
 * address range (by PC); stack addresses (locals) are skipped here. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SITES 8192
#define KEEP 8

typedef struct {
    uint64_t count, distinct_n;
    int64_t smin, smax;
    uint64_t umin, umax;
    uint64_t zero, neg, smaxhit, umaxhit, nzpos, nzneg;
    uint64_t values[KEEP];
} Range;

typedef struct {
    uintptr_t pc;
    int kind; /* 1 cmp, 2 read */
    int size, is_const;
    uint64_t eq, slt, sgt, ult, ugt, gtpos, gtneg, gtmin;
    uintptr_t addr;
    int same;
    Range a, b;
} Site;

static Site sites[SITES];
static Range args[16];
static int arg_kind[16], arg_bits[16];
static int enabled;
static uintptr_t stack_low, stack_high;

static int64_t sext(uint64_t v, int bits) {
    if (bits >= 64) return (int64_t)v;
    uint64_t m = 1ull << (bits - 1);
    v &= (1ull << bits) - 1;
    return (int64_t)((v ^ m) - m);
}

static void range_add(Range *r, uint64_t v, int bits) {
    uint64_t mask = bits >= 64 ? ~0ull : (1ull << bits) - 1;
    v &= mask;
    int64_t s = sext(v, bits);
    if (r->count == 0) {
        r->smin = r->smax = s;
        r->umin = r->umax = v;
    }
    if (s < r->smin) r->smin = s;
    if (s > r->smax) r->smax = s;
    if (v < r->umin) r->umin = v;
    if (v > r->umax) r->umax = v;
    r->count++;
    if (v == 0) r->zero++;
    if (s < 0) r->neg++;
    if (v != 0 && s > 0) r->nzpos++;
    if (v != 0 && s < 0) r->nzneg++;
    if (v == (mask >> 1)) r->smaxhit++;
    if (v == mask) r->umaxhit++;
    if (r->distinct_n <= KEEP) {
        for (uint64_t i = 0; i < r->distinct_n && i < KEEP; i++)
            if (r->values[i] == v) return;
        if (r->distinct_n < KEEP) r->values[r->distinct_n] = v;
        r->distinct_n++;
    }
}

static Site *site(uintptr_t pc, int kind) {
    uint64_t h = ((uint64_t)pc * 0x9E3779B97F4A7C15ull) >> 51;
    for (int i = 0; i < SITES; i++) {
        Site *s = &sites[(h + i) % SITES];
        if (s->pc == pc && s->kind == kind) return s;
        if (s->pc == 0) {
            s->pc = pc;
            s->kind = kind;
            return s;
        }
    }
    return NULL;
}

static void cmp(uintptr_t pc, uint64_t a, uint64_t b, int bits, int is_const) {
    if (!enabled) return;
    Site *s = site(pc, 1);
    if (!s) return;
    s->size = bits / 8;
    s->is_const = is_const;
    int64_t sa = sext(a, bits), sb = sext(b, bits);
    uint64_t mask = bits >= 64 ? ~0ull : (1ull << bits) - 1;
    a &= mask;
    b &= mask;
    if (a == b) s->eq++;
    if (sa < sb) s->slt++;
    if (sa > sb) s->sgt++;
    if (a < b) s->ult++;
    if (a > b) {
        s->ugt++;
        if (sa >= 0) s->gtpos++; else s->gtneg++;
        if (s->gtpos + s->gtneg == 1 || a < s->gtmin) s->gtmin = a;
    }
    range_add(&s->a, a, bits);
    range_add(&s->b, b, bits);
}

#define PC ((uintptr_t)__builtin_return_address(0))
void __sanitizer_cov_trace_cmp1(uint8_t a, uint8_t b) { cmp(PC, a, b, 8, 0); }
void __sanitizer_cov_trace_cmp2(uint16_t a, uint16_t b) { cmp(PC, a, b, 16, 0); }
void __sanitizer_cov_trace_cmp4(uint32_t a, uint32_t b) { cmp(PC, a, b, 32, 0); }
void __sanitizer_cov_trace_cmp8(uint64_t a, uint64_t b) { cmp(PC, a, b, 64, 0); }
/* The constant is the first operand: record the variable as `a`. */
void __sanitizer_cov_trace_const_cmp1(uint8_t k, uint8_t v) { cmp(PC, v, k, 8, 1); }
void __sanitizer_cov_trace_const_cmp2(uint16_t k, uint16_t v) { cmp(PC, v, k, 16, 1); }
void __sanitizer_cov_trace_const_cmp4(uint32_t k, uint32_t v) { cmp(PC, v, k, 32, 1); }
void __sanitizer_cov_trace_const_cmp8(uint64_t k, uint64_t v) { cmp(PC, v, k, 64, 1); }
void __sanitizer_cov_trace_cmpf(float a, float b) { (void)a; (void)b; }
void __sanitizer_cov_trace_cmpd(double a, double b) { (void)a; (void)b; }
/* A switch: cases[0] is the case count, cases[1] the value's bit width. */
void __sanitizer_cov_trace_switch(uint64_t v, uint64_t *cases) {
    if (!enabled) return;
    Site *s = site(PC, 1);
    if (!s) return;
    int bits = (int)cases[1];
    s->size = bits / 8;
    s->is_const = 2;
    for (uint64_t i = 0; i < cases[0]; i++)
        if (cases[2 + i] == v) s->eq++;
    range_add(&s->a, v, bits);
}

static void read_value(uintptr_t pc, const void *p, int size) {
    if (!enabled) return;
    uintptr_t a = (uintptr_t)p;
    if (a >= stack_low && a < stack_high) return; /* a local */
    Site *s = site(pc, 2);
    if (!s) return;
    uint64_t v = 0;
    memcpy(&v, p, size > 8 ? 8 : size);
    if (s->a.count == 0) {
        s->addr = a;
        s->same = 1;
    } else if (s->addr != a) {
        s->same = 0;
    }
    s->size = size;
    range_add(&s->a, v, size >= 8 ? 64 : size * 8);
}

void __tsan_init(void) {}
void __tsan_func_entry(void *pc) { (void)pc; }
void __tsan_func_exit(void) {}
void __tsan_read1(void *p) { read_value(PC, p, 1); }
void __tsan_read2(void *p) { read_value(PC, p, 2); }
void __tsan_read4(void *p) { read_value(PC, p, 4); }
void __tsan_read8(void *p) { read_value(PC, p, 8); }
void __tsan_read16(void *p) { read_value(PC, p, 16); }
void __tsan_unaligned_read2(void *p) { read_value(PC, p, 2); }
void __tsan_unaligned_read4(void *p) { read_value(PC, p, 4); }
void __tsan_unaligned_read8(void *p) { read_value(PC, p, 8); }
void __tsan_unaligned_read16(void *p) { read_value(PC, p, 16); }
void __tsan_volatile_read1(void *p) { read_value(PC, p, 1); }
void __tsan_volatile_read2(void *p) { read_value(PC, p, 2); }
void __tsan_volatile_read4(void *p) { read_value(PC, p, 4); }
void __tsan_volatile_read8(void *p) { read_value(PC, p, 8); }
void __tsan_read_range(void *p, unsigned long n) { (void)p; (void)n; }
void __tsan_write1(void *p) { (void)p; }
void __tsan_write2(void *p) { (void)p; }
void __tsan_write4(void *p) { (void)p; }
void __tsan_write8(void *p) { (void)p; }
void __tsan_write16(void *p) { (void)p; }
void __tsan_unaligned_write2(void *p) { (void)p; }
void __tsan_unaligned_write4(void *p) { (void)p; }
void __tsan_unaligned_write8(void *p) { (void)p; }
void __tsan_unaligned_write16(void *p) { (void)p; }
void __tsan_volatile_write1(void *p) { (void)p; }
void __tsan_volatile_write2(void *p) { (void)p; }
void __tsan_volatile_write4(void *p) { (void)p; }
void __tsan_volatile_write8(void *p) { (void)p; }
void __tsan_write_range(void *p, unsigned long n) { (void)p; (void)n; }
void __tsan_vptr_update(void *p, void *v) { (void)p; (void)v; }
void __tsan_vptr_read(void *p) { (void)p; }

/* main.zig: probes on around the C run only (the stubs and the harness's
 * own work are not the function's). `stack_top` is the harness's frame. */
void zdiff_cov_enable(int on, uintptr_t stack_top) {
    enabled = on;
    stack_high = stack_top + 4096;
    stack_low = stack_top - (8u << 20);
}

void zdiff_cov_arg(int index, int kind, int bits, uint64_t value) {
    if (index < 0 || index >= 16) return;
    arg_kind[index] = kind;
    arg_bits[index] = bits;
    range_add(&args[index], value, bits ? bits : 64);
}

static void put_range(FILE *f, const Range *r) {
    fprintf(f, " %lld %lld %llu %llu %llu %llu %llu %llu %llu", (long long)r->smin, (long long)r->smax,
            (unsigned long long)r->umin, (unsigned long long)r->umax, (unsigned long long)r->zero,
            (unsigned long long)r->neg, (unsigned long long)r->smaxhit, (unsigned long long)r->umaxhit,
            (unsigned long long)r->distinct_n);
    for (uint64_t i = 0; i < r->distinct_n && i < KEEP; i++) fprintf(f, " %llu", (unsigned long long)r->values[i]);
}

void zdiff_cov_dump(void) {
    const char *path = getenv("ZDIFF_COV_OUT");
    if (!path) return;
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < SITES; i++) {
        const Site *s = &sites[i];
        if (!s->pc) continue;
        if (s->kind == 1) {
            fprintf(f, "cmp %#lx %d %d %llu %llu %llu %llu %llu %llu %lld %lld %lld %lld %llu %llu %llu %llu %llu %llu %llu %llu",
                    (unsigned long)s->pc, s->size, s->is_const, (unsigned long long)s->a.count,
                    (unsigned long long)s->eq, (unsigned long long)s->slt, (unsigned long long)s->sgt,
                    (unsigned long long)s->ult, (unsigned long long)s->ugt, (long long)s->a.smin,
                    (long long)s->a.smax, (long long)s->b.smin, (long long)s->b.smax,
                    (unsigned long long)s->a.zero, (unsigned long long)s->a.neg, (unsigned long long)s->a.nzpos,
                    (unsigned long long)s->a.nzneg, (unsigned long long)s->gtpos, (unsigned long long)s->gtneg, (unsigned long long)s->gtmin,
                    (unsigned long long)s->a.distinct_n);
            for (uint64_t k = 0; k < s->a.distinct_n && k < KEEP; k++)
                fprintf(f, " %llu", (unsigned long long)s->a.values[k]);
        } else {
            fprintf(f, "read %#lx %d %llu %#lx %d", (unsigned long)s->pc, s->size, (unsigned long long)s->a.count,
                    (unsigned long)s->addr, s->same);
            put_range(f, &s->a);
        }
        fputc('\n', f);
    }
    for (int i = 0; i < 16; i++) {
        if (!args[i].count) continue;
        fprintf(f, "arg %d %d %d %llu", i, arg_kind[i], arg_bits[i], (unsigned long long)args[i].count);
        put_range(f, &args[i]);
        fputc('\n', f);
    }
    fclose(f);
}
