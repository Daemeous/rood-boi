#include <stdio.h>
#include <string.h>
#include <stdlib.h>
unsigned char *D_800F5878[17], *vs_battle_actors[17];
unsigned D_800F58B8, D_800F58BC, D_800F590C, D_800F58D0, D_800F58C0;
void* D_800F5920;
static int routes, commands, removed, freed;
static void* freed_values[4];
void func_800E5240(unsigned id)
{
    if (id != 1)
        abort();
    ++routes;
}
void func_80095B7C(int id, int command)
{
    if (id != 1 || command != (commands ? 0x40 : 0))
        abort();
    ++commands;
}
void func_800DBF00(void* state)
{
    if (state != D_800F5878[1])
        abort();
    ++removed;
}
void vs_main_freeHeap(void* ptr) { freed_values[freed++] = ptr; }
void func_800E7454(void*);
void func_800E6F1C(void);
void func_800E678C(void*);
void func_800E4C28(int, int);
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void)
{
    unsigned actors[2][32] = { { 0 } }, states[2][0x484 / 4] = { { 0 } },
             grid[32] = { 0 };
    unsigned short cells[32 * 32];
    memset(cells, 0xff, sizeof(cells));
    unsigned char* node = (void*)actors[1];
    unsigned char* state = (void*)states[1];
    unsigned char* g = (void*)grid;
    actors[0][0] = (unsigned)node;
    actors[1][1] = 1;
    vs_battle_actors[0] = (void*)actors[0];
    D_800F5878[0] = (void*)states[0];
    D_800F5878[1] = state;
    D_800F5878[0][0x16] = 1;
    state[0x16] = 2;
    D_800F58BC = (unsigned)grid;
    D_800F590C = 6;
    node[0x1c] = 5;
    node[0xb] = 0xff;
    node[0x1a] = 0xac;
    state[0x88] = 1;
    state[0x170] = 3;
    state[0x172] = 4;
    state[0x1a] = 9;
    g[0] = 31;
    g[2] = 31;
    *(unsigned short*)(g + 0x10) = 6;
    g[0x12] = 2;
    *(void**)(g + 0x28) = node;
    *(void**)(g + 0x34) = actors[0];
    D_800F5920 = cells;
    func_800E7454(node); /* Inactive navigation leaves all state intact. */
    CHECK(D_800F590C == 6 && !routes && !freed);
    D_800F58B8 = (unsigned)cells;
    func_800E7454(node);
    CHECK(D_800F590C == 4 && D_800F5878[0][0x16] == 16 && state[0x16] == 2);
    CHECK(*(void**)(g + 0x28) == 0 && *(void**)(g + 0x34) == actors[0]);
    CHECK(cells[4 * 32 + 3] == 0xff7f && cells[4 * 32 + 2] == 0xffff && state[0x1a] == 0);
    CHECK(*(unsigned short*)(g + 0x10) == 4 && g[0x12] == 1 && node[0xb] == 0xfc);
    CHECK(routes == 1 && commands == 2 && removed == 1 && freed == 2);
    CHECK(freed_values[0] == state && freed_values[1] == cells && !D_800F5920
          && !D_800F5878[1]);
    state[0x170] = 32;
    state[0x19b] = 1;
    g[0xe] = 3;
    func_800E678C(state);
    CHECK(g[0xe] == 0 && cells[4 * 32 + 31] == 0xffff);
    /* Remaining actor list is visited without freeing its actor nodes. */
    actors[0][0] = 0;
    D_800F58D0 = 123;
    D_800F58C0 = 456;
    func_800E6F1C();
    CHECK(freed == 3 && freed_values[2] == grid);
    CHECK(!D_800F58D0 && !D_800F58B8 && !D_800F58C0 && !D_800F58BC);
    puts("PASS: actor unload references, occupancy, flags, callbacks and heap release");
    return 0;
}
