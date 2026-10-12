/* Probe for pc_route_continuation (766FC.s L800DF8A4) and its two 7BDD4.s
 * entries func_800E4B2C / func_800E4B70, linked against the real
 * pc/src/battle_movement.c (func_800D954C, func_800DEEFC command 14,
 * func_800E4BD8) with a scratchpad, a navigation grid and a room record.
 * func_800DFAF8 (771A8.c, C) and the movement fallbacks are stubs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

unsigned char *D_800F4538[17], *vs_battle_actors[17];
unsigned D_800F58B8, D_800F58BC, D_800F5910;
extern unsigned pc_route_continuations;
void pc_route_continuation(void*, unsigned);
void func_800E4B2C(void*, int);
void func_800E4B70(void*, int);

void pc_unimplemented(const char* s)
{
    fprintf(stderr, "unexpected trap: %s\n", s);
    exit(2);
}
int ratan2(int x, int z) { return (x * 3 + z) & 0xfff; }
int rcos(int a) { return a; }
int rsin(int a) { return a; }

static unsigned char state[0x484], command[0x20];
static int link_index, link_calls, recovered, held, freed_moves, walked;
int func_800DFAF8(int self, unsigned exit, int near)
{
    if (self != (int)(unsigned)state || exit != 0x00050004 || near != 0)
        abort();
    ++link_calls;
    return link_index;
}
void func_800E2CCC(void* p, int angle)
{
    (void)angle;
    if (p != state)
        abort();
    ++recovered;
}
void func_800E1908(void* p, int angle) { held += p == state && angle == 33; }
void func_800E3600(void* p, int angle, int mode) { freed_moves += p == state && angle == 33 && mode == 1; }
void func_800E19FC(void* p, int angle) { walked += p == state && angle == 0; }

#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static unsigned short cells[1024], floors[1024], links[16];
static unsigned char room[0x40];

static void reset(int gate, unsigned tile)
{
    memset(state, 0, sizeof state);
    memset(command, 0, sizeof command);
    *(unsigned*)(state + 0x54) = (unsigned)command;
    *(unsigned*)(command + 4) = 1; /* the node's actor id */
    state[0x88] = 1;
    *(unsigned short*)(state + 0xb0) = 0x140; /* step height */
    *(unsigned*)(state + 0x224) = 0x00250042; /* walking, speed 0x25 */
    *(unsigned*)(state + 0x34) = tile;
    *(unsigned*)(state + 0x38) = 0x00010001; /* reserved tile elsewhere */
    *(unsigned*)(state + 0x19c) = 0x00050004;
    state[0x19b] = gate;
    room[0xe] = 0xff;
    link_calls = recovered = 0;
}

int main(void)
{
    unsigned char* scratch = mmap((void*)0x1f800000, 4096, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    CHECK(scratch != MAP_FAILED);
    *(unsigned*)(scratch + 0x3bc) = (unsigned)floors;
    *(unsigned*)(scratch + 0x3c0) = (unsigned)cells;
    scratch[0x3dc] = scratch[0x3dd] = 32;
    D_800F58B8 = (unsigned)cells;
    D_800F58BC = (unsigned)room;
    D_800F5910 = (unsigned)links;
    links[12] = 7 | (9 << 5); /* link 6's near end: tile (7, 9) */

    /* Off a link tile (byte 1 < 2): release the gate, forget the far end. */
    reset(4, 0x00030102);
    pc_route_continuation(state, 0x80123456);
    CHECK(room[0xe] == 0xef && state[0x19b] == 0 && *(int*)(state + 0x19c) == -1);
    CHECK(!link_calls && !recovered && command[9] == 0 && *(unsigned*)(state + 0x170) == 0);

    /* On a link tile: the link's near end is free, so step onto it
     * (command 14 = 0xB here: same height, no flags), releasing gate
     * (6 >> 1) + 2 = 5. Byte 3 of the step is the caller's $s2 byte. */
    reset(3, 0x00030202);
    link_index = 6;
    pc_route_continuation(state, 0x80123456);
    CHECK(link_calls == 1 && !recovered);
    CHECK(*(unsigned*)(state + 0x170) == 0x80090007 && command[9] == 0xb && command[8] == 0x80);
    CHECK(*(unsigned*)(command + 0xc) == 0x80090007 && *(unsigned*)(command + 0x10) == 0x25);
    CHECK(*(unsigned*)(scratch + 0x3c8) == 14 && (cells[9 * 32 + 7] & 0x80));
    CHECK(room[0xe] == 0xdf && state[0x19b] == 0 && *(int*)(state + 0x19c) == -1);
    CHECK(*(unsigned*)(state + 0x174) == 7 * 128 + 64 && *(unsigned short*)(state + 0x178) == 9 * 128 + 64);

    /* Near end blocked (func_800D954C 0): recover, the gate stays held. */
    reset(3, 0x00030202);
    pc_route_continuation(state, 0);
    CHECK(link_calls == 1 && recovered == 1 && state[0x19b] == 3 && room[0xe] == 0xff && command[9] == 0);
    cells[9 * 32 + 7] = 0;

    /* No link leads to the far end: recover. */
    reset(3, 0x00030202);
    link_index = -1;
    pc_route_continuation(state, 0);
    CHECK(link_calls == 1 && recovered == 1 && state[0x19b] == 3 && command[9] == 0);

    /* The 7BDD4.s entries: with a gate, both continue the route (byte 3 0);
     * a held actor (0x1A) takes func_800E1908 first; without a gate the
     * usual moves run. */
    unsigned before = pc_route_continuations;
    reset(3, 0x00030202);
    link_index = 6;
    func_800E4B2C(state, 33);
    CHECK(pc_route_continuations == before + 1 && *(unsigned*)(state + 0x170) == 0x00090007 && !walked);
    cells[9 * 32 + 7] = 0;
    reset(3, 0x00030202);
    func_800E4B70(state, 33);
    CHECK(pc_route_continuations == before + 2 && command[9] == 0xb && !freed_moves);
    cells[9 * 32 + 7] = 0;
    reset(3, 0x00030202);
    state[0x1a] = 1;
    func_800E4B70(state, 33);
    CHECK(pc_route_continuations == before + 2 && held == 1);
    reset(0, 0x00030202);
    func_800E4B70(state, 33);
    CHECK(freed_moves == 1 && state[2] == 1);
    reset(0, 0x00030202);
    *(unsigned*)(state + 0x168) = 0x00030002;
    func_800E4B2C(state, 0);
    CHECK(recovered == 1 && pc_route_continuations == before + 2);
    *(unsigned*)(state + 0x168) = 0x00040002;
    func_800E4B2C(state, 0);
    CHECK(walked == 1 && *(unsigned*)(state + 0x17c) == 2 * 128 + 64 && *(unsigned*)(state + 0x180) == 4 * 128 + 64);
    munmap(scratch, 4096);
    puts("PASS: route continuation (L800DF8A4): gate release off links, step onto a link's near end, recovery, "
         "func_800E4B2C/func_800E4B70 entries");
    return 0;
}
