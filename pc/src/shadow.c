/* The wrapper owns callback arguments and return values; this transaction
 * owns memory rollback and a C setjmp boundary around the owned callback. */
#include "shadow.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int active, speculative, owned_side;
static int compare_impl(const char*, unsigned, pc_shadow_call, pc_shadow_call,
    pc_shadow_call, int (*)(void*), void*, const pc_state_range*, int, pc_shadow_result*);
static jmp_buf panic_boundary;
static pc_shadow_result* pending;
static pc_shadow_result last;
static char fault_function[128];
static uintptr_t fault_address;
int pc_oracle_classic(void);
int pc_shadow_set_fault(const char* function, uintptr_t address)
{
    if (active || !pc_oracle_classic() || !function || strlen(function) >= sizeof fault_function)
        return -1;
    if (!*function && !address) {
        fault_function[0] = 0;
        fault_address = 0;
        return 0;
    }
    pc_state_range ranges[170];
    int count = pc_state_ranges(ranges, 170), valid = 0;
    if (count < 1 || count > 170 || !*function) return -1;
    for (int i = 0; i < count; i++) {
        uintptr_t base = (uintptr_t)ranges[i].start;
        valid |= address >= base && address - base < ranges[i].size;
    }
    if (!valid) return -1;
    strcpy(fault_function, function);
    fault_address = address;
    return 0;
}
static unsigned calls, mismatches, errors, logged, dropped;
static pc_shadow_result log_entries[32];
static struct { const char* name; unsigned roots, nested; } coverage[128];
static unsigned covered;
static void cover(const char* name)
{
    unsigned i;
    for (i = 0; i < covered; i++)
        if (!strcmp(coverage[i].name, name)) break;
    if (i == 128) return;
    if (i == covered) {
        coverage[i].name = name;
        coverage[i].roots = coverage[i].nested = 0;
        ++covered;
    }
    if (active) ++coverage[i].nested;
    else ++coverage[i].roots;
}
static void record_result(void)
{
    mismatches += last.mismatch != 0;
    errors += last.error != 0;
    if (last.mismatch || last.error) {
        if (logged < 32) log_entries[logged++] = last;
        else ++dropped;
    }
}
extern char* pc_game_stack(size_t*) __attribute__((weak));
extern unsigned pc_vsync_count(void) __attribute__((weak));
const pc_shadow_result* pc_shadow_last(void) { return &last; }
unsigned pc_shadow_calls(void) { return calls; }

int pc_shadow_route(const char* name, uintptr_t frame, pc_shadow_call original,
    pc_shadow_call repeat, pc_shadow_call owned, int (*equal)(void*), void* context)
{
    ++calls;
    cover(name);
    if (active) {
        if (owned_side) owned(context);
        else original(context);
        return owned_side;
    }
    size_t size = 0;
    char* stack = pc_game_stack ? pc_game_stack(&size) : NULL;
    uintptr_t base = (uintptr_t)stack;
    uintptr_t start = frame + 2 * sizeof(uintptr_t);
    memset(&last, 0, sizeof last);
    last.function = name;
    last.frame = pc_vsync_count ? pc_vsync_count() : 0;
    if (!stack || size > UINTPTR_MAX - base || start < frame || start < base || start > base + size) {
        last.error = -1;
        snprintf(last.message, sizeof last.message, "caller stack boundary unavailable");
        original(context);
        record_result();
        return 0;
    }
    pc_state_range output = {(char*)start, base + size - start, "caller.stack"};
    if (compare_impl(name, last.frame, original, repeat, owned, equal, context, &output, 1, &last)) {
        original(context);
        record_result();
        return 0;
    }
    record_result();
    return 0;
}

/* JSON escapes all bytes outside printable ASCII, including panic text. */
static int quoted(char* out, int size, const char* text)
{
    int n = 0;
    if (size < 3) return -1;
    out[n++] = '"';
    for (const unsigned char* p = (const unsigned char*)(text ? text : ""); *p; p++) {
        if (n > size - 8) return -1;
        if (*p == '"' || *p == '\\') {
            out[n++] = '\\';
            out[n++] = (char)*p;
        } else if (*p < 32 || *p >= 127) {
            n += snprintf(out + n, (size_t)(size - n), "\\u%04x", *p);
        } else out[n++] = (char)*p;
    }
    out[n++] = '"';
    out[n] = 0;
    return n;
}
int pc_shadow_report(char* out, int size, int reset)
{
    if (!out || size < 128 || active) return -1;
    int n = snprintf(out, (size_t)size,
        "{\"calls\":%u,\"count\":%u,\"errors\":%u,\"truncated\":%s,\"mismatches\":[",
        calls, mismatches, errors, dropped ? "true" : "false");
    for (unsigned i = 0; i < logged; i++) {
        pc_shadow_result* r = &log_entries[i];
        char function[1024], range[600], message[1600];
        if (quoted(function, sizeof function, r->function) < 0 ||
            quoted(range, sizeof range, r->difference.range) < 0 ||
            quoted(message, sizeof message, r->message) < 0) return -1;
        int used = snprintf(out + n, (size_t)(size - n),
            "%s{\"function\":%s,\"frame\":%u,\"range\":%s,\"first\":%llu,\"offset\":%zu,"
            "\"expected\":%u,\"actual\":%u,\"panic\":%s,\"error\":%d,\"message\":%s}",
            i ? "," : "", function, r->frame, range, (unsigned long long)r->difference.address,
            r->difference.offset, r->difference.expected, r->difference.actual,
            r->panicked ? "true" : "false", r->error, message);
        if (used < 0 || used >= size - n) return -1;
        n += used;
    }
    if (size - n < 16) return -1;
    memcpy(out + n, "],\"coverage\":[", 14);
    n += 14;
    for (unsigned i = 0; i < covered; i++) {
        char function[1024];
        if (quoted(function, sizeof function, coverage[i].name) < 0) return -1;
        int used = snprintf(out + n, (size_t)(size - n),
            "%s{\"function\":%s,\"calls\":%u,\"nested\":%u}",
            i ? "," : "", function, coverage[i].roots, coverage[i].nested);
        if (used < 0 || used >= size - n) return -1;
        n += used;
    }
    if (size - n < 3) return -1;
    memcpy(out + n, "]}", 3);
    if (reset) calls = mismatches = errors = logged = dropped = covered = 0;
    return n + 2;
}

int pc_oracle_classic(void)
{
    static int classic = -1;
    if (classic < 0) {
        const char* env = getenv("ROOD_ORACLE");
        classic = env && !strcmp(env, "1");
    }
    return classic;
}
int pc_shadow_available(void)
{
#if defined(__linux__) && defined(__x86_64__)
    return pc_oracle_classic();
#else
    return 0;
#endif
}
int pc_shadow_speculative(void) { return speculative; }
int pc_shadow_panic(const char* message, size_t length)
{
    if (!speculative || !pending)
        return 0;
    size_t n = length < sizeof pending->message - 1 ? length : sizeof pending->message - 1;
    memcpy(pending->message, message, n);
    pending->message[n] = 0;
    pending->panicked = pending->mismatch = 1;
    longjmp(panic_boundary, 1);
}

static void restore(const pc_state_image* image)
{
    if (pc_state_restore(image)) {
        fputs("[shadow] fatal: cannot restore oracle state\n", stderr);
        abort();
    }
}
static void copy_outputs(const pc_state_range* outputs, int count, unsigned char* bytes, int write)
{
    size_t at = 0;
    for (int i = 0; i < count; i++) {
        if (outputs[i].size) {
            if (write)
                memcpy(outputs[i].start, bytes + at, outputs[i].size);
            else
                memcpy(bytes + at, outputs[i].start, outputs[i].size);
        }
        at += outputs[i].size;
    }
}
/* Touch dead stack before each reference execution. Volatile prevents the
 * optimizer dropping the fill; the live caller and guard are never touched. */
__attribute__((noinline)) static void poison_stack(unsigned char pattern)
{
    volatile unsigned char bytes[16384];
    for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = pattern;
}
static int compare_impl(const char* function, unsigned frame,
    pc_shadow_call original, pc_shadow_call repeat, pc_shadow_call owned,
    int (*equal)(void*), void* context,
    const pc_state_range* outputs, int count, pc_shadow_result* result)
{
    if (!result)
        return -1;
    memset(result, 0, sizeof *result);
    result->function = function;
    result->frame = frame;
    result->error = -1;
    if (active || !function || !original || !owned || count < 0 || count > 64 || (count && !outputs))
        return -1;
    size_t size = 0;
    for (int i = 0; i < count; i++) {
        uintptr_t start = (uintptr_t)outputs[i].start;
        if ((outputs[i].size && !start) || outputs[i].size > UINTPTR_MAX - start ||
            outputs[i].size > SIZE_MAX - size)
            return -1;
        for (int j = 0; j < i; j++)
            if (start < (uintptr_t)outputs[j].start + outputs[j].size &&
                (uintptr_t)outputs[j].start < start + outputs[i].size)
                return -1;
        size += outputs[i].size;
    }
    pc_state_image* before = pc_state_capture();
    unsigned char* initial = malloc(size ? size : 1);
    unsigned char* expected = malloc(size ? size : 1);
    if (!before || !initial || !expected) {
        pc_state_image_free(before);
        free(initial);
        free(expected);
        return -1;
    }
    pc_state_image* reference = NULL;
    unsigned char* repeated_outputs = NULL;
    int stable_return = 1;
    copy_outputs(outputs, count, initial, 0);
    active = 1;
    if (repeat) poison_stack(0x55);
    original(context);
    pc_state_image* oracle = pc_state_capture();
    copy_outputs(outputs, count, expected, 0);
    if (!oracle) {
        /* Original already executed: do not ask the caller to execute it again. */
        result->error = -2;
        goto cleanup;
    }
    if (repeat) {
        repeated_outputs = malloc(size ? size : 1);
        if (!repeated_outputs) {
            result->error = -2;
            goto cleanup;
        }
        restore(before);
        copy_outputs(outputs, count, initial, 1);
        speculative = 1; /* suppress host effects, but nested routes stay original */
        poison_stack(0xaa);
        repeat(context);
        speculative = 0;
        stable_return = !equal || equal(context);
        reference = pc_state_capture();
        copy_outputs(outputs, count, repeated_outputs, 0);
        if (!reference) {
            result->error = -2;
            restore(oracle);
            copy_outputs(outputs, count, expected, 1);
            goto cleanup;
        }
    }
    restore(before);
    copy_outputs(outputs, count, initial, 1);
    pending = result;
    owned_side = 1;
    speculative = 1;
    if (setjmp(panic_boundary) == 0)
        owned(context);
    speculative = owned_side = 0;
    pending = NULL;
    if (!result->panicked && fault_address && !strcmp(function, fault_function))
        *(unsigned char*)fault_address ^= 1;
    pc_state_image* actual = pc_state_capture();
    result->error = actual ? 0 : -2;
    if (actual) {
        int different = pc_state_image_stable_difference(oracle, reference, actual, &result->difference);
        if (different < 0)
            result->error = -2;
        else
            result->mismatch |= different;
    }
    if (!result->mismatch) {
        size_t at = 0;
        for (int i = 0; i < count && !result->mismatch; i++) {
            for (size_t j = 0; j < outputs[i].size; j++) {
                unsigned char value = (unsigned char)outputs[i].start[j];
                if (value != expected[at + j] &&
                    (!repeated_outputs || expected[at + j] == repeated_outputs[at + j])) {
                    result->mismatch = 1;
                    result->difference.address = (uintptr_t)outputs[i].start + j;
                    result->difference.offset = j;
                    result->difference.expected = expected[at + j];
                    result->difference.actual = value;
                    snprintf(result->difference.range, sizeof result->difference.range, "%s",
                        outputs[i].name ? outputs[i].name : "caller");
                    break;
                }
            }
            at += outputs[i].size;
        }
    }
    if (!result->error && !result->panicked && stable_return && equal && !equal(context)) {
        if (!result->mismatch)
            snprintf(result->difference.range, sizeof result->difference.range, "return");
        result->mismatch = 1;
    }
    pc_state_image_free(actual);
    restore(oracle);
    copy_outputs(outputs, count, expected, 1);
cleanup:
    pc_state_image_free(reference);
    free(repeated_outputs);
    pc_state_image_free(oracle);
    pc_state_image_free(before);
    free(initial);
    free(expected);
    active = 0;
    return 0;
}
int pc_shadow_compare(const char* function, unsigned frame,
    pc_shadow_call original, pc_shadow_call owned, void* context,
    const pc_state_range* outputs, int count, pc_shadow_result* result)
{
    return compare_impl(function, frame, original, NULL, owned, NULL, context, outputs, count, result);
}
