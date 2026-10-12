/* 7DDDC.s and 7C7E0.s: actor navigation frame preparation. Native C keeps
 * its own stack; scratchpad data layout and explicit missing callees remain. */
#include "psx_types.h" /* psx_load/psx_store: 4-byte guest pointers */
extern unsigned char *D_800F5878[], *D_800F4538[], *vs_battle_actors[];
extern unsigned D_800F58BC, D_800F58D0, D_800F58B8, D_800F58CC, D_800F58C0, D_800F58C4;
extern unsigned D_800F16E0, D_800F16E4, D_800F16E8, D_800F16EC, D_800F16F0;
extern void pc_unimplemented(const char*);
extern int func_800E45B4(void);
extern int func_800E4660(int, int, int);
extern void func_800DEEFC(void*, int);
extern void func_800E50A0(void*, int, int);
extern void func_800D9538(void);
extern void vs_main_freeHeapR(void*);
extern int ratan2(int, int);
extern void func_800E19FC(void*, int);
extern void* vs_main_allocHeapR(unsigned);
extern void* vs_main_memcpy(void*, const void*, unsigned);
extern void func_800E4288(void*, unsigned);
extern void func_800D82CC(void*);
extern void func_800D821C(void*);
extern void func_800DBF00(void*);
extern void pc_route_continuation(void*, unsigned); /* battle_movement.c */

void pc_actor_scratch(unsigned* scratch)
{
    for (int i = 16; i >= 0; --i)
        scratch[(0x37c / 4) + i] = psx_to_int(unsigned, D_800F5878[i]);
    scratch[0x3bc / 4] = D_800F58D0;
    scratch[0x3c0 / 4] = D_800F58B8;
    scratch[0x3c4 / 4] = D_800F58CC;
    scratch[0x3f4 / 4] = D_800F58C0;
    scratch[0x3f8 / 4] = D_800F58C4;
    scratch[0x3e0 / 4] = D_800F16E0;
    scratch[0x3e4 / 4] = D_800F16E4;
    scratch[0x3e8 / 4] = D_800F16E8;
    scratch[0x3ec / 4] = D_800F16EC;
    scratch[0x3f0 / 4] = D_800F16F0;
    /* Without a navigation record (func_800E5308 does not test it, unlike
     * func_800E511C) the original reads word 4 of address 0: PS1 RAM, the
     * kernel area, read here through guest RAM. */
    scratch[0x3dc / 4] = *psx_to_ptr(unsigned*, (D_800F58BC ? D_800F58BC : 0x80000000u) + 4);
}
void func_800E65DC(void) { pc_actor_scratch((unsigned*)0x1f800000); }
void func_800E5240(unsigned id)
{
    if (id >= 17)
        pc_unimplemented("actor navigation index");
    unsigned char* state = D_800F5878[id];
    if (!D_800F58B8 || !state)
        return;
    void* route = psx_load(void*, state + 0x18c);
    if (!route)
        return;
    psx_store(state + 0x18c, (void*)0);
    vs_main_freeHeapR(route);
}
/* func_800E5154: func_800E5158 with $a3 = 1. 0 when actor `id`'s route
 * record is on its target tile, 1 while it is elsewhere, 2 when there is no
 * navigation grid, state or route. (The game's C caller passes no argument;
 * the PS1 relied on $a0 still holding the actor id, see 146C.c.) */
int func_800E5154(unsigned id)
{
    if (id >= 17)
        pc_unimplemented("actor navigation index");
    unsigned char* state = D_800F5878[id];
    if (!D_800F58B8 || !state)
        return 2;
    unsigned char* route = psx_load(unsigned char*, state + 0x18c);
    if (!route)
        return 2;
    unsigned tile = *psx_load(unsigned*, route + 0x64) & 0xff00ff;
    return tile != (*(unsigned*)(route + 0x168) & 0xff00ff);
}
/* func_800E51C8: copy actor `id`'s navigation state into `route` and reset
 * its route fields. */
static void route_init(unsigned id, unsigned char* route)
{
    vs_main_memcpy(route, D_800F5878[id], 0x484);
    route[0x88] = id;
    *(short*)(route + 0xb0) = 0x140;
    route[0xb2] = 3;
    *(unsigned*)(route + 0x190) = 0;
    route[0x19a] = 0;
    route[0x29] = 3;
    route[0x23] = 1;
}
/* func_800E527C: send actor `id` towards (x, z). Releases its reservations,
 * then allocates its route record on first use (0x484 bytes, heap 7) and
 * initializes it with the target. Returns 0 without a grid, state or memory.
 * As in the assembly, an existing route record is left alone: the state
 * record is copied onto itself and takes the target instead. */
int func_800E527C(unsigned id, int x, int z)
{
    unsigned target = ((unsigned)z << 16) | (unsigned)x;
    if (id >= 17)
        pc_unimplemented("actor navigation index");
    unsigned char* state = D_800F5878[id];
    if (!D_800F58B8 || !state)
        return 0;
    func_800D82CC(state);
    func_800D821C(state);
    func_800DBF00(state);
    unsigned char* record = state;
    if (!psx_load(void*, state + 0x18c)) {
        record = vs_main_allocHeapR(0x484);
        if (!record)
            return 0;
        psx_store(state + 0x18c, record);
        func_800E4288(record, 0x484);
    }
    route_init(state[0x88], record);
    *(unsigned*)(record + 0x160) = target;
    return 1;
}
static void route_target(unsigned char* state)
{
    if (state[0x19b]) {
        /* L800E550C -> L800DF8A4; the original's $s2 is this route record. */
        pc_route_continuation(state, psx_to_int(unsigned, state));
        return;
    }
    unsigned target = *(unsigned*)(state + 0x160);
    unsigned x = target & 65535, z = target >> 16;
    *(unsigned*)(state + 0x17c) = x;
    *(unsigned*)(state + 0x180) = z;
    *(unsigned*)(state + 0x168) = (x >> 7) | ((z >> 7) << 16);
    int angle =
        ratan2((int)x - *(short*)(state + 0x4c), (int)z - *(short*)(state + 0x50));
    func_800E19FC(state, angle);
}
void func_800E5308(void)
{
    func_800E65DC();
    unsigned* scratch = (unsigned*)0x1f800000;
    unsigned char* actor = vs_battle_actors[0];
    for (unsigned visited = 0; actor; actor = psx_load(unsigned char*, actor)) {
        if (++visited > 17)
            pc_unimplemented("actor navigation list cycle");
        if (actor[8] & 0xc0)
            continue;
        unsigned id = *(unsigned*)(actor + 4);
        if (id >= 17)
            pc_unimplemented("actor navigation index");
        /* An actor without an AI state (no attribute bit 0/2) has a null
         * entry; the original then loads its route record from address
         * 0x18C, the PS1 kernel area (read here through guest RAM). A
         * nonzero word there would have the PS1 write into its kernel, so
         * on a working PS1 this reads 0 and the actor is skipped. */
        unsigned char* base = psx_to_ptr(unsigned char*, scratch[0x37c / 4 + id]);
        if (!base)
            base = psx_to_ptr(unsigned char*, 0x80000000u);
        unsigned char* state = psx_load(unsigned char*, base + 0x18c);
        scratch[0x3fc / 4] = func_800E45B4();
        if (!state)
            continue;
        state[0x19a] = 0;
        unsigned short* countdown = (void*)(state + 0x156);
        if (*countdown)
            --*countdown;
        unsigned flags = *(unsigned*)(state + 0x190), steps = (flags >> 5) & 127;
        if (steps)
            --steps;
        state[6] = 1;
        state[1] = 0;
        state[0x15c] = 16;
        state[2] = 0;
        state[3] = 0;
        actor[3] &= 0xfc;
        *(unsigned*)(state + 0x190) = (flags & ~0xfe0u) | (steps << 5);
        unsigned char* model = D_800F4538[id];
        if (!model)
            pc_unimplemented("actor navigation model missing");
        unsigned tile = *(unsigned*)(model + 0x5c);
        unsigned x = *(unsigned*)(model + 0x1c), z = *(unsigned*)(model + 0x20);
        *(unsigned*)(state + 0x34) = tile;
        *(unsigned*)(state + 0x4c) = x;
        *(unsigned*)(state + 0x50) = z;
        unsigned cell = ((tile >> 16) & 255) * 32 + (tile & 255);
        if (cell >= 1024)
            pc_unimplemented("actor navigation tile outside grid");
        unsigned ground = psx_to_ptr(unsigned short*, D_800F58B8)[cell] & 15;
        x &= 65535;
        z &= 65535;
        *(unsigned*)(state + 0x120) = (x >> 6) | ((z >> 6) << 16);
        state[0x132] = state[0x154] = ground;
        scratch[0x3d0 / 4] = 0;
        unsigned target = *(unsigned*)(state + 0x160);
        if (!state[0x125]) {
            if (func_800E4660((target & 65535) - x, 0, (target >> 16) - z) >= 1024) {
                route_target(state);
                continue;
            }
        } else if ((*(unsigned*)(state + 0x168) & 0xff00ff) != (tile & 0xff00ff)) {
            *(unsigned*)(state + 0x168) =
                ((target & 65535) >> 7) | ((target >> 23) << 16);
            func_800E50A0(state, 0, 0);
            continue;
        }
        func_800DEEFC(state, 0);
        func_800E5240(state[0x88]);
    }
    func_800D9538();
}
