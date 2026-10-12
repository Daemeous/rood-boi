#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
unsigned char *D_800F5878[17], *D_800F4538[17], *vs_battle_actors[17];
unsigned D_800F58BC, D_800F58D0, D_800F58B8, D_800F58CC, D_800F58C0, D_800F58C4;
unsigned D_800F16E0 = 1, D_800F16E4 = 2, D_800F16E8 = 3, D_800F16EC = 4, D_800F16F0 = 5;
void pc_actor_scratch(unsigned*);
void func_800E5308(void);
static unsigned char route[0x484], base[0x484], model[0x100];
static int finish, freed, moved, updated;
void pc_unimplemented(const char* s)
{
    fprintf(stderr, "unexpected trap: %s\n", s);
    exit(2);
}
int func_800E45B4(void) { return 123; }
int func_800E4660(int x, int y, int z)
{
    return (short)x * (short)x + (short)y * (short)y + (short)z * (short)z;
}
void func_800DEEFC(void* p, int n)
{
    if (p != route || n)
        abort();
    ++finish;
}
void func_800E50A0(void* p, int a, int b)
{
    if (p != route || a || b)
        abort();
    ++updated;
}
void func_800D9538(void) { }
void vs_main_freeHeapR(void* p)
{
    if (p != route)
        abort();
    ++freed;
}
int ratan2(int x, int z)
{
    if (x != 64 || z)
        abort();
    return 77;
}
void func_800E19FC(void* p, int angle)
{
    if (p != route || angle != 77)
        abort();
    ++moved;
}
static unsigned continued, continued_s2;
void pc_route_continuation(void* p, unsigned s2)
{
    if (p != route)
        abort();
    ++continued;
    continued_s2 = s2;
}
static unsigned char heap[0x484];
static int released, allocated;
void func_800D82CC(void* p) { released += p == base; }
void func_800D821C(void* p) { released += p == base; }
void func_800DBF00(void* p) { released += p == base; }
void* vs_main_allocHeapR(unsigned size)
{
    if (size != 0x484)
        abort();
    ++allocated;
    memset(heap, 0xee, sizeof heap);
    return heap;
}
void* vs_main_memcpy(void* d, const void* s, unsigned n) { return memmove(d, s, n); }
void func_800E4288(void* d, unsigned n) { memset(d, 0, n); }
int func_800E527C(unsigned, int, int);
int func_800E5154(unsigned);
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    void* mapped = mmap((void*)0x1f800000, 4096, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    CHECK(mapped != MAP_FAILED);
    unsigned grid[8] = { 0, 0x12345678 }, node[4] = { 0 };
    unsigned short tiles[1024] = { 0 };
    D_800F58BC = (unsigned)grid;
    D_800F58B8 = (unsigned)tiles;
    D_800F58D0 = 0x1234;
    D_800F5878[0] = base;
    D_800F4538[0] = model;
    vs_battle_actors[0] = (void*)node;
    pc_actor_scratch(mapped);
    unsigned* scratch = mapped;
    CHECK(scratch[0x37c / 4] == (unsigned)base && scratch[0x3bc / 4] == 0x1234);
    CHECK(scratch[0x3dc / 4] == 0x12345678 && scratch[0x3f0 / 4] == 5);
    func_800E5308();
    CHECK(scratch[0x3fc / 4] == 123 && !finish);
    *(void**)(base + 0x18c) = route;
    *(unsigned*)(model + 0x1c) = 128;
    *(unsigned*)(model + 0x20) = 256;
    *(unsigned*)(model + 0x5c) = 0x00020001;
    tiles[65] = 7;
    *(unsigned*)(route + 0x160) = 128 | (256u << 16);
    *(unsigned*)(route + 0x190) = 0xa0000060;
    *(unsigned short*)(route + 0x156) = 2;
    func_800E5308();
    CHECK(finish == 1 && freed == 1 && *(void**)(base + 0x18c) == 0);
    CHECK(*(unsigned short*)(route + 0x156) == 1
          && *(unsigned*)(route + 0x190) == 0xa0000040);
    CHECK(route[0x132] == 7 && route[0x154] == 7
          && *(unsigned*)(route + 0x120) == (2 | (4u << 16)));
    *(void**)(base + 0x18c) = route;
    *(unsigned*)(route + 0x160) = 192 | (256u << 16);
    func_800E5308();
    CHECK(moved == 1 && *(unsigned*)(route + 0x17c) == 192
          && *(unsigned*)(route + 0x180) == 256);
    /* Holding a gate (0x19B), the far-away target continues the route
     * through it (L800E550C -> L800DF8A4, $s2 = the route record). */
    *(void**)(base + 0x18c) = route;
    route[0x19b] = 3;
    *(unsigned*)(route + 0x17c) = 0;
    func_800E5308();
    CHECK(continued == 1 && continued_s2 == (unsigned)route && moved == 1 && !*(unsigned*)(route + 0x17c));
    route[0x19b] = 0;
    route[0x125] = 1;
    *(unsigned*)(route + 0x168) = 0;
    func_800E5308();
    CHECK(updated == 1 && continued == 1);
    /* Route requests: a record on first use, initialized from the state. */
    *(void**)(base + 0x18c) = 0;
    base[0x88] = 0;
    base[0x40] = 0x5a;
    CHECK(func_800E5154(0) == 2);
    CHECK(func_800E527C(0, 0x123, 0x456) == 1 && released == 3 && allocated == 1);
    CHECK(*(void**)(base + 0x18c) == heap && heap[0x40] == 0x5a);
    CHECK(*(unsigned*)(heap + 0x160) == (0x123 | (0x456u << 16)));
    CHECK(*(short*)(heap + 0xb0) == 0x140 && heap[0xb2] == 3 && heap[0x29] == 3
          && heap[0x23] == 1 && *(unsigned*)(heap + 0x190) == 0);
    /* A second request keeps the record; as in the assembly the target goes
     * to the state record. */
    CHECK(func_800E527C(0, 7, 9) == 1 && allocated == 1);
    CHECK(*(unsigned*)(heap + 0x160) == (0x123 | (0x456u << 16)));
    CHECK(*(unsigned*)(base + 0x160) == (7 | (9u << 16)));
    unsigned on_tile = 0x00030002;
    *(void**)(heap + 0x64) = &on_tile;
    *(unsigned*)(heap + 0x168) = 0x77030002;
    CHECK(func_800E5154(0) == 0);
    *(unsigned*)(heap + 0x168) = 0x00030003;
    CHECK(func_800E5154(0) == 1);
    D_800F58B8 = 0;
    CHECK(func_800E527C(0, 1, 1) == 0 && func_800E5154(0) == 2);
    munmap(mapped, 4096);
    puts(
        "PASS: actor scratch cache, inactive routes, countdown, terrain, route finish and target updates, route requests");
    return 0;
}
