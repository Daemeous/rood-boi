/* The engine library's heap (build_boot.py --engine-library only): its malloc family goes through the host's registry, so that what the library
 * still holds when it is unloaded is freed then (engine_host.c rood_alloc_release). The library's statics die with it: the lists, meshes, caches and
 * Lua state it allocated would otherwise stay in the process for good, a few MB per reload. Linked with -Wl,--wrap for these names. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

extern void rood_alloc_note(void* p, size_t n); /* engine_host.c */
extern void rood_alloc_forget(void* p);
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
void __real_free(void*);
char* __real_strdup(const char*);
char* __real_strndup(const char*, size_t);
int __real_posix_memalign(void**, size_t, size_t);
void* __real_aligned_alloc(size_t, size_t);
void* __real_memalign(size_t, size_t);

void* __wrap_malloc(size_t n)
{
    void* p = __real_malloc(n);
    if (p)
        rood_alloc_note(p, n);
    return p;
}
void* __wrap_calloc(size_t a, size_t b)
{
    void* p = __real_calloc(a, b);
    if (p)
        rood_alloc_note(p, a * b);
    return p;
}
void* __wrap_realloc(void* p, size_t n)
{
    void* q = __real_realloc(p, n);
    if (!q && n)
        return NULL; /* (p is still there) */
    if (p)
        rood_alloc_forget(p);
    if (q)
        rood_alloc_note(q, n);
    return q;
}
void __wrap_free(void* p)
{
    if (p)
        rood_alloc_forget(p); /* (before the free: the address may be handed out again at once) */
    __real_free(p);
}
char* __wrap_strdup(const char* s)
{
    char* p = __real_strdup(s);
    if (p)
        rood_alloc_note(p, strlen(p) + 1);
    return p;
}
char* __wrap_strndup(const char* s, size_t n)
{
    char* p = __real_strndup(s, n);
    if (p)
        rood_alloc_note(p, strlen(p) + 1);
    return p;
}
int __wrap_posix_memalign(void** out, size_t align, size_t n)
{
    int r = __real_posix_memalign(out, align, n);
    if (!r && *out)
        rood_alloc_note(*out, n);
    return r;
}
void* __wrap_aligned_alloc(size_t align, size_t n)
{
    void* p = __real_aligned_alloc(align, n);
    if (p)
        rood_alloc_note(p, n);
    return p;
}
void* __wrap_memalign(size_t align, size_t n)
{
    void* p = __real_memalign(align, n);
    if (p)
        rood_alloc_note(p, n);
    return p;
}
