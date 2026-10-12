/* Native equivalents of the hand-written actor movement-state assembly in
 * BATTLE.PRG 72B70.s and its 7BDD4.s/7C7E0.s helpers. Guest structure offsets
 * keep the original ABI; return values reproduce the original $v0 where C
 * callers test them. */
#include "psx_types.h" /* psx_load/psx_store: 4-byte guest pointers */
extern unsigned char *D_800F5878[], *D_800F4538[], *vs_battle_actors[];
extern unsigned D_800F58D8; /* a 32-bit guest address */
/* Scratchpad bytes and words; pointers kept there are 4-byte guest addresses
 * (SCRATCH_PTR), like every pointer stored inside the PS1-layout actor and AI
 * records read below by offset (psx_load/psx_store). */
#define SCRATCH_AT(offset) psx_to_ptr(unsigned char*, 0x1f800000 + (offset))
#define SCRATCH8(offset) (*SCRATCH_AT(offset))
#define SCRATCH32(offset) (*(unsigned*)SCRATCH_AT(offset))
#define SCRATCH_PTR(T, offset) psx_load(T, SCRATCH_AT(offset))
extern void pc_unimplemented(const char*);
extern void pc_require_native_callback(unsigned, const char*, unsigned);
extern int func_800DAD9C(void*);
extern int func_800DB0BC(void*);
extern void func_800E1EAC(void*, unsigned, int);

/* 7BDD4.s: rebuild the movement flags at 0x190: keep bits outside 0xFE1
 * (mask 0xFFFFF01E) and set 0x3C0 when the signed byte at 0x23 is nonzero. */
void func_800E4BB8(void* state)
{
    unsigned char* base = state;
    unsigned flags = *(unsigned*)(base + 0x190) & 0xfffff01e;
    if ((signed char)base[0x23])
        flags |= 0x3c0;
    *(unsigned*)(base + 0x190) = flags;
}

/* 72B70.s: enter movement mode `mode` (byte 0x89 always records the request).
 * Re-entering the current mode (0x8A) does nothing. Modes reset their own
 * fields; mode 5 picks the nearest flagged waypoint of the actor's set
 * (D_800F58D8, 8 halfwords; bit 15 marks an entry, bits 0-4/5-9 its tile)
 * by Manhattan distance to the actor tile at 0x34. Returns the original $v0,
 * which is always nonzero. */
int func_800DB4AC(void* state, int mode)
{
    unsigned char* base = state;
    int result = -1;
    base[0x89] = mode;
    if (base[0x8a] == mode)
        return mode;
    if (mode == 1) {
        *(unsigned*)(base + 0xe4) = 0;
        *(unsigned short*)(base + 0xe8) = 0;
        result = 3;
    } else if (mode == 3) {
        *(unsigned short*)(base + 0xc0) = 0;
        *(unsigned short*)(base + 0xd2) = 0;
        result = 6;
    } else if (mode == 6) {
        base[0x10c] = 0;
        *(int*)(base + 0x108) = -1;
    } else if (mode == 5) {
        unsigned best = 0xffffffffu, tile = *(unsigned*)(base + 0x34);
        int tile_x = tile & 0xff, tile_z = (tile >> 16) & 0xff, choice = -1;
        short* entries = psx_to_ptr(short*, D_800F58D8 + base[0x101] * 16);
        for (int i = 0; i < 8; ++i) {
            int entry = entries[i];
            if (entry >= 0)
                continue;
            int dx = (entry & 0x1f) - tile_x, dz = (((unsigned)entry >> 5) & 0x1f) - tile_z;
            unsigned distance = (unsigned)((dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz));
            if (distance < best) {
                best = distance;
                choice = i;
            }
        }
        if (choice >= 0)
            base[0x100] = choice;
        base[0x104] = 0;
        result = 1;
    }
    base[0x8a] = mode;
    base[0x474 + mode * 2] = 0;
    func_800E4BB8(base);
    return result;
}

/* 72B70.s: func_800DB5B8 enters mode 3 and wanders around (tile, range);
 * func_800DB5B0 does so with the state's own 0xCC/0xC8 fields. */
void func_800DB5B8(void* state, unsigned tile, int range)
{
    func_800DB4AC(state, 3);
    func_800E1EAC(state, tile, range);
}
void func_800DB5B0(void* state)
{
    unsigned char* base = state;
    func_800DB5B8(state, *(unsigned*)(base + 0xcc), *(int*)(base + 0xc8));
}

/* 72B70.s: func_800DB5DC enters mode 2 to walk to `tile` (stored at 0x168):
 * within `range` of the tile centre (L800E5030 in 7C7E0.s; height from
 * func_800E42E0, squared distances compared unsigned) the walk finishes
 * (func_800DEEFC 0), otherwise it moves (func_800E50A0 0, heading 0). */
extern int func_800E42E0(unsigned);
extern void func_800DEEFC(void*, int);
extern void func_800E50A0(void*, int, int);
extern int func_800E4660(int, int, int);
void func_800DB5DC(void* state, unsigned tile, int range)
{
    unsigned char* base = state;
    *(unsigned*)(base + 0x168) = tile;
    func_800DB4AC(state, 2);
    tile = *(unsigned*)(base + 0x168);
    int y = func_800E42E0(tile);
    unsigned distance = (unsigned)func_800E4660((int)((tile & 0xff) << 7) + 64 - *(short*)(base + 0x4c),
        y - *(short*)(base + 0x4e), (int)(((tile >> 16) & 0xff) << 7) + 64 - *(short*)(base + 0x50));
    if (distance < (unsigned)(range * range))
        func_800DEEFC(state, 0);
    else
        func_800E50A0(state, 0, 0);
}
/* 72B70.s: func_800DB5FC enters mode 5 and patrols its waypoints
 * (func_800E23AC). The assembly skips both when $a2 is 0; its only caller
 * (func_800DAA6C) passes no such argument and the PS1 value is whatever
 * func_800DA1D4 left there, nonzero on most of its paths (3, 0x280, the grid
 * size), so the port always patrols. */
extern void func_800E23AC(void*);
void func_800DB5FC(void* state)
{
    func_800DB4AC(state, 5);
    func_800E23AC(state);
}

/* 72B70.s: per-frame movement decision. Dispatch (func_800DAD9C), unstick
 * (func_800DB0BC) and the state's own callback at 0x68 each may claim the
 * frame; otherwise the actor wanders. The callback must be a native function. */
void func_800DB820(void* state)
{
    unsigned char* base = state;
    if (func_800DAD9C(state) || func_800DB0BC(state))
        return;
    int (*callback)(void*) = psx_load(int (*)(void*), base + 0x68);
    if (callback) {
        pc_require_native_callback(psx_to_int(unsigned, callback), "actor movement callback", 0);
        if (callback(state))
            return;
    }
    func_800DB5B0(state);
}

extern unsigned char* D_800F45E0[];
/* Guest words at 0x800F58xx hold 32-bit guest addresses (see psx_load). */
extern unsigned D_800F58BC, D_800F58B8;
extern int func_800E4660(int, int, int);
extern int vs_gte_rsqrt(int);

/* 7BDD4.s: squared tile distance between two packed tiles (x byte 0, z byte 2). */
int func_800E45F4(unsigned a, unsigned b)
{
    return func_800E4660((int)(a & 0xff) - (int)(b & 0xff), 0,
        (int)((a >> 16) & 0xff) - (int)((b >> 16) & 0xff));
}

/* 73460.s: candidate needs its target engaged: 1 when the state is busy
 * (byte 0x13), 0 without candidate option bit 4, else whether the target has
 * no engagement (D_800F45E0[target] null). */
int func_800DBC60(void* state, void* candidate)
{
    unsigned* entry = candidate;
    if (((unsigned char*)state)[0x13])
        return 1;
    if (!(entry[4] & 0x10))
        return 0;
    return !D_800F45E0[entry[3] & 0xf];
}
/* 73460.s: candidate allowed now: not while the halfword at 0x90 is set, and
 * not when func_800DBC60 holds and the timer at 0x13F is running. */
int func_800DBCB4(void* state, void* candidate)
{
    unsigned char* base = state;
    if (*(unsigned short*)(base + 0x90))
        return 0;
    if (!func_800DBC60(state, candidate))
        return 1;
    return !base[0x13f];
}

/* PS1 main RAM through any of its KUSEG/KSEG0/KSEG1 mirrors (2 MB repeated
 * up to 8 MB), for the original's out-of-list reads. The game's data,
 * overlays and heap keep their PS1 addresses there; an unaligned or non-RAM
 * address would raise an address/bus error on the PS1. */
static unsigned guest_word(unsigned address)
{
    if ((address & 0x1fffffffu) >= 0x00800000u || (address & 3))
        pc_unimplemented("actor engagement read outside guest RAM");
    return *psx_to_ptr(unsigned*, 0x80000000u | (address & 0x1fffff));
}
/* 73460.s: 1 unless some hostile actor (relation >= 3) stands within two
 * tiles of the engaged target of `id`. The original starts its list walk at
 * &vs_battle_actors itself, so the first "node" has id vs_battle_actors[1]
 * (a pointer, or 0); its relation word (0x1A4 + id * 4, wrapping) and, when
 * that reads as hostile, its model D_800F4538[id] (0x800F4538 + id * 4) and
 * the model's tile (+0x5C) are read through guest RAM as the PS1 does. */
int func_800DBCEC(void* state, int id)
{
    unsigned char* base = state;
    unsigned char* node = (unsigned char*)vs_battle_actors;
    for (; node; node = psx_load(unsigned char*, node)) {
        unsigned other = *(unsigned*)(node + 4);
        unsigned relation_address = psx_to_int(unsigned, base) + 0x1a4 + (other << 2);
        unsigned relation = other < 16 ? *(unsigned*)(base + 0x1a4 + other * 4)
                                       : guest_word(relation_address);
        if (((relation >> 6) & 3) < 3)
            continue;
        unsigned tile = other < 16 ? *(unsigned*)(D_800F4538[other] + 0x5c)
                                   : guest_word(guest_word(0x800f4538u + (other << 2)) + 0x5c);
        if (func_800E45F4(tile, *(unsigned*)(D_800F45E0[id] + 0x5c)) < 3)
            return 0;
    }
    return 1;
}

/* 73580.s: whether candidate action `candidate` can start now (weight,
 * threshold against the room value at D_800F58BC + 0x18, availability, target
 * engagement, distance and range, relation and model flags). The original
 * calls vs_gte_rsqrt on the distance and discards the result; the call is kept
 * for its GTE side effects. */
int func_800DBD80(void* state, void* candidate)
{
    unsigned char* base = state;
    unsigned* entry = candidate;
    unsigned target = entry[3] & 0xf;
    if (!((entry[3] >> 19) & 0xff))
        return 0;
    if (*psx_to_ptr(unsigned short*, D_800F58BC + 0x18) < ((entry[1] >> 13) & 0xffff))
        return 0;
    if (!func_800DBCB4(state, candidate))
        return 0;
    int limit = *(int*)(base + 0x1e4 + target * 4);
    if (D_800F45E0[target]) {
        if (0x19000 < limit || base[0x13] || func_800DBCEC(state, target))
            return 0;
    }
    int distance = (int)entry[2];
    int relation = *(int*)(base + 0x1a4 + target * 4);
    if (distance < limit)
        return 0;
    vs_gte_rsqrt(distance);
    unsigned options = entry[4];
    int range = (options << 10) >> 16;
    if (range > 0 && (options & 2) && limit < (int)((unsigned)range * range))
        return 0;
    if (relation >= 0)
        return 0;
    unsigned model = D_800F4538[0][0xa];
    if ((model & 7) || (model & 0x18) == 8)
        return 0;
    if (!(((options >> 2) & 1) | ((relation >> 2) & 1)))
        return 0;
    if (!(((options >> 3) & 1) | !((relation >> 27) & 1)))
        return 0;
    return 1;
}

extern unsigned* func_8008E2D4(int);
extern void func_800DFA54(int, unsigned, unsigned, void*);
extern int func_800E24EC(void*, int);
extern int func_800E2B2C(void*, int);
extern int ratan2(int, int);
extern void func_800E2CCC(void*, int);
extern void func_800E19FC(void*, int);
extern void func_800E1908(void*, int);
extern void func_800E3600(void*, int, int);
extern void func_800E0A68(void*, int);
extern void func_800E1238(void*, int);
extern void func_800E153C(void*, int);
extern void func_800E1764(void*, int);

extern void func_800E4BD8(int);
extern int func_800DFAF8(int, unsigned, int);
extern int func_800D954C(void*, unsigned);
extern void func_800DEEFC(void*, int);
extern unsigned D_800F5910; /* guest address of the room's u16 link pairs */
/* 766FC.s L800DF8A4: continue a route through a room link (gate). Byte 0x19B
 * is the gate bit the actor holds in the room flags (func_800E4BE0 by
 * func_800DFBCC), word 0x19C the far end it is heading to. Off a link tile
 * (tile byte 1 < 2) the gate is released (func_800E4BD8), the far end reset
 * to -1 and 0x19B cleared. On a link tile, func_800DFAF8 finds the link whose
 * far end is 0x19C; when its near end (even halfword `index` * 2 of
 * D_800F5910: x bits 0-4, z bits 5-9) can be entered (func_800D954C > 0), the
 * gate of pair index / 2 is released, the step is stored at 0x170 and command
 * 14 issued; otherwise func_800E2CCC recovers.
 * The original builds the step tile in the caller's $s2 (it saves $s2 but
 * never initialises it), so byte 3 of 0x170 is the caller's top $s2 byte:
 * `caller_s2` passes it. func_800D954C shifts byte 3 out and the command
 * consumers (func_80075554, func_800A208C, func_800B6580) read bytes 0-2
 * only, so the value is unobservable; callers pass what they know. */
unsigned pc_route_continuations; /* times run (tests read it by symbol) */
void pc_route_continuation(void* state, unsigned caller_s2)
{
    unsigned char* base = state;
    ++pc_route_continuations;
    if (base[0x35] < 2) {
        *(int*)(base + 0x19c) = -1;
        func_800E4BD8(base[0x19b]);
        base[0x19b] = 0;
        return;
    }
    int index = func_800DFAF8(psx_to_int(int, base), *(unsigned*)(base + 0x19c), 0);
    if (index >= 0) {
        unsigned link = psx_to_ptr(unsigned short*, D_800F5910)[index * 2];
        unsigned tile = (caller_s2 & 0xff000000u) | (link & 0x1f) | (((link >> 5) & 0x1f) << 16);
        if (func_800D954C(state, tile) > 0) {
            *(int*)(base + 0x19c) = -1;
            func_800E4BD8((index >> 1) + 2);
            base[0x19b] = 0;
            *(unsigned*)(base + 0x170) = tile;
            func_800DEEFC(state, 14);
            return;
        }
    }
    func_800E2CCC(state, 0);
}

/* 7BDD4.s: head for the packed target tile at 0x168 unless already on it
 * (then func_800E2CCC); with a gate held (0x19B) continue the route through
 * it instead (L800DF8A4). The callers' $s2 is not known natively; its byte
 * is unobservable (see pc_route_continuation). */
void func_800E4B2C(void* state, int angle)
{
    unsigned char* base = state;
    if (base[0x19b]) {
        pc_route_continuation(state, 0);
        return;
    }
    unsigned target = *(unsigned*)(base + 0x168) & 0xff00ff;
    if (target == (*(unsigned*)(base + 0x34) & 0xff00ff)) {
        func_800E2CCC(state, angle);
        return;
    }
    *(unsigned*)(base + 0x17c) = (target & 0xff) * 128 + 64;
    *(unsigned*)(base + 0x180) = (target >> 9) + 64;
    func_800E19FC(state, angle);
}
/* 7BDD4.s: free movement (func_800E3600 mode 1, angle & 0xFFF) unless the
 * actor is held (0x1A); with a gate held (0x19B) continue the route through
 * it (L800DF8A4, returning through 0x800E4BAC). */
void func_800E4B70(void* state, int angle)
{
    unsigned char* base = state;
    if (base[0x1a]) {
        func_800E1908(state, angle);
        return;
    }
    if (base[0x19b]) {
        pc_route_continuation(state, 0);
        return;
    }
    base[2] = 1;
    *(unsigned char*)(0x1f800000 + 0x3d0) = 0;
    func_800E3600(state, angle & 0xfff, 1);
}
/* 7C7E0.s: dispatch through jtbl_800F1740 by priority state (0x125) and
 * `kind`, passing `angle`. The original table holds these six functions. */
void func_800E50A0(void* state, int kind, int angle)
{
    static void (*const table[6])(void*, int) = { func_800E4B2C, func_800E4B70,
        func_800E0A68, func_800E1238, func_800E153C, func_800E1764 };
    unsigned index = ((unsigned char*)state)[0x125] * 2 + kind;
    if (index >= 6)
        pc_unimplemented("actor movement dispatch index");
    table[index](state, angle);
}
/* 7C7E0.s: aim at the point (0x17C, 0x180), record it packed at 0x158, mark
 * movement (bytes 1 = kind, 9 = 1) and dispatch with the heading. */
void func_800E4FE0(void* state, int kind)
{
    unsigned char* base = state;
    int x = *(int*)(base + 0x17c), z = *(int*)(base + 0x180);
    base[1] = kind;
    base[9] = 1;
    *(unsigned*)(base + 0x158) = (unsigned)x | ((unsigned)z << 16);
    func_800E50A0(state, 1,
        ratan2(x - *(short*)(base + 0x4c), z - *(short*)(base + 0x50)));
}

/* 72B70.s: approach actor `id`. Uses the model's waypoint group when allowed,
 * otherwise its tile; then plans with func_800E24EC and on success enters
 * mode 1. A missing cached actor still enters mode 1 for nonzero ids, as the
 * original's leftover $v0 (id * 4) does. */
int func_800DB370(void* state, int id, int distance, int option)
{
    unsigned char* base = state;
    unsigned char* model = D_800F4538[id];
    unsigned cached = ((unsigned*)(0x1f800000 + 0x37c))[id];
    int result;
    if (!cached) {
        result = id * 4;
    } else {
        int busy = *(short*)(base + 0x15e) & 0x38;
        int group = 0, grouped = 0;
        if (base[0xe5] != id) {
            *(unsigned*)(base + 0xe4) = 0;
            *(unsigned*)(base + 0xe8) = 0;
        }
        base[0xe5] = id;
        *(int*)(base + 0xdc) = distance;
        if (!busy && (model[0xa] & 0x1f)) {
            unsigned tile = *(unsigned*)(model + 0x60);
            *(unsigned*)(base + 0x168) = tile;
            if (tile & 0xff00) {
                group = (int)((tile & 0xff00) >> 8) - 2;
                grouped = 1;
            }
        } else if ((base[0x29] | busy) && (model[0xc] & 0xf)) {
            group = (model[0xc] & 0xf) - 2;
            grouped = 1;
        } else {
            *(unsigned*)(base + 0x168) = *(unsigned*)(model + 0x5c);
        }
        if (grouped) {
            unsigned* waypoint = func_8008E2D4(group);
            func_800DFA54(group, waypoint[0], waypoint[2], base + 0x168);
            unsigned tile = *(unsigned*)(base + 0x168);
            *(unsigned*)(base + 0xec) = tile;
            *(unsigned*)(base + 0xf4) = ((tile & 0xff) << 1) | (((tile >> 16) & 0xff) << 1);
        }
        result = func_800E24EC(state, option);
    }
    return result ? func_800DB4AC(state, 1) : 0;
}
/* 72B70.s: aim at actor `id`'s position and tile; on a plan enter mode 6. */
int func_800DB74C(void* state, int id)
{
    unsigned char* base = state;
    unsigned char* model = D_800F4538[id];
    *(int*)(base + 0x17c) = *(short*)(model + 0x1c);
    *(int*)(base + 0x180) = *(short*)(model + 0x20);
    *(unsigned*)(base + 0x168) = *(unsigned*)(model + 0x5c);
    return func_800E2B2C(state, id) ? func_800DB4AC(state, 6) : 0;
}
/* 72B70.s: flee toward the (logically halved, running) average tile of hostile
 * actors (relation bits 5-7 == 7), remembering it for 90 frames at 0x110/0x114;
 * while in mode 4 keep going there until the timer runs out. */
int func_800DB61C(void* state, int kind)
{
    unsigned char* base = state;
    unsigned sum_x = 0, sum_z = 0;
    int count = 0;
    for (unsigned char* node = vs_battle_actors[0]; node; node = psx_load(unsigned char*, node)) {
        int id = *(int*)(node + 4);
        if (((*(unsigned*)(base + 0x1a4 + id * 4) >> 5) & 7) < 7)
            continue;
        unsigned char* model = D_800F4538[id];
        ++count;
        sum_x += *(short*)(model + 0x1c);
        sum_z += *(short*)(model + 0x20);
        if (count != 1) {
            sum_x >>= 1;
            sum_z >>= 1;
        }
    }
    if (count > 0) {
        *(unsigned short*)(base + 0x114) = 90;
        *(unsigned*)(base + 0x110) = (sum_x >> 7) | ((sum_z >> 7) << 16);
    } else {
        unsigned timer = *(unsigned short*)(base + 0x114);
        if (timer)
            --timer;
        *(unsigned short*)(base + 0x114) = timer;
        if (!timer || base[0x8a] != 4)
            return 0;
    }
    unsigned tile = *(unsigned*)(base + 0x110);
    base[4] = 1;
    *(unsigned*)(base + 0x17c) = (tile & 0xff) * 128 + 64;
    *(unsigned*)(base + 0x180) = ((tile >> 16) & 0xff) * 128 + 64;
    *(unsigned*)(base + 0x168) = tile;
    func_800E4FE0(state, kind);
    return func_800DB4AC(state, 4);
}
/* 72B70.s: first other actor whose relation bits 6-7 equal `relation`; stores
 * its object's tile in *tile when given. Returns its id or -1. */
int func_800DBB2C(void* state, unsigned* tile, int relation)
{
    unsigned char* base = state;
    for (unsigned char* node = vs_battle_actors[0]; node; node = psx_load(unsigned char*, node)) {
        int id = *(int*)(node + 4);
        if (id == base[0x88] || (*(unsigned*)(base + 0x1a4 + id * 4) & 0xc0) != (unsigned)relation << 6)
            continue;
        if (tile)
            *tile = *(unsigned*)(psx_load(unsigned char*, node + 0x44) + 0x5c);
        return id;
    }
    return -1;
}

extern int func_800DA1D4(void*);
extern int func_800DA5CC(void*);
extern int func_800DA6A4(void*);
extern int func_800DA738(void*);
extern int func_800DAA6C(void*);
extern int func_800DAAD8(void*);
extern int func_800D826C(void*);
extern void func_800DB5DC(void*, unsigned, int);

/* 72B70.s behaviour 0x800DBA4C: wander unless fleeing (0x0E set), else
 * func_800DA1D4. */
static int behaviour_800DBA4C(void* state)
{
    if (!((unsigned char*)state)[0xe])
        return func_800DA1D4(state);
    if (!func_800DB61C(state, 1))
        func_800DB5B0(state);
    return 1;
}
/* 72B70.s behaviour 0x800DBA88: flee when 0x0E is set; for stances 0/1 (0x131)
 * approach the best candidate's target within 0x100 (0x140 for models with
 * flag 0x20), stance 0 capping that by the candidate's rsqrt distance. */
static int behaviour_800DBA88(void* state)
{
    unsigned char* base = state;
    if (base[0xe])
        return func_800DB61C(state, 1);
    unsigned stance = (1u << base[0x131]) & 3;
    if (!stance)
        return func_800DA1D4(state);
    unsigned* best = psx_load(unsigned*, base + 0x430);
    int limit = (psx_load(unsigned char*, base + 0x58))[0xa] & 0x20 ? 0x140 : 0x100;
    unsigned target = best[3] & 0xf;
    if (stance & 1) {
        int root = vs_gte_rsqrt((int)best[2]);
        if (!(limit < root))
            limit = root;
    }
    int result = func_800DB370(state, target, limit, 0);
    return result ? result : func_800DB74C(state, target);
}
/* 72B70.s behaviour 0x800DBB98: unless func_800DA1D4 claims the frame, wander
 * near the first hostile actor (relation 2): far ones (> 0x64000) by tile
 * (func_800DB5DC), near ones by area (func_800DB5B8, range 5). */
static int behaviour_800DBB98(void* state)
{
    unsigned char* base = state;
    unsigned tile;
    int result = func_800DA1D4(state);
    if (result)
        return result;
    int id = func_800DBB2C(state, &tile, 2);
    if (id < 0)
        return 0;
    if (0x64000u < *(unsigned*)(base + 0x1e4 + id * 4))
        func_800DB5DC(state, tile, 0);
    else
        func_800DB5B8(state, tile, 5);
    return 1;
}
/* 72B70.s behaviour D_800DBC0C: when func_800D826C holds let func_800DA1D4
 * claim the frame; otherwise approach actor 0 within 0x100. */
static int behaviour_800DBC0C(void* state)
{
    if (func_800D826C(state)) {
        int result = func_800DA1D4(state);
        if (result)
            return result;
    }
    int result = func_800DB370(state, 0, 0x100, 0);
    return result ? result : func_800DB74C(state, 0);
}
/* Native D_800F1790 (actor behaviour table indexed by actor flags bits 4-8);
 * build_boot.py verifies the original words before linking. */
int (*D_800F1790[])(void*) = { behaviour_800DBA4C, behaviour_800DBA88, behaviour_800DBB98,
    func_800DA5CC, func_800DA6A4, func_800DA738, func_800DAA6C, func_800DAAD8,
    behaviour_800DBC0C, 0 };

/* 72B70.s: whether a candidate's target counts as engaged in the room's
 * current encounter (D_800F58BC): forced by 0x8E, blocked by 0x98, otherwise
 * the target relation bit 0 plus the room's counters and per-actor mask. The
 * original calls func_800DBCB4 and discards its result. */
int func_800DB8AC(void* state, void* candidate)
{
    unsigned char* base = state;
    unsigned char* room = psx_to_ptr(unsigned char*, D_800F58BC);
    unsigned* entry = candidate;
    func_800DBCB4(state, candidate);
    if (*(unsigned short*)(base + 0x8e))
        return 1;
    if (*(unsigned short*)(base + 0x98))
        return 0;
    if (!(*(unsigned*)(base + 0x1a4 + (entry[3] & 0xf) * 4) & 1))
        return 0;
    unsigned count = room[0xd];
    if (count < *(unsigned*)(room + 0x24))
        return 0;
    if (*(unsigned short*)(room + 0x10) & (1u << base[0x88]))
        return 1;
    return !(count < room[0x12]);
}

extern unsigned D_800F58E0;
extern void func_800DBF00(void*);
/* 6FA6C.s: join the room's encounter. Already engaged (0x8E) succeeds;
 * a full encounter (count 0x0D below members 0x12) or a block (0x98 or
 * D_800F58E0) fails. Otherwise set the engagement timer (1, or 0x20 after a
 * recent leave at 0x9A), add the actor's bit to mask 0x10, bump members and
 * push the actor on the room's engaged list (head 0x1C, tail 0x20, link 0x1A0). */
int func_800D836C(void* state)
{
    unsigned char* base = state;
    unsigned char* room = psx_to_ptr(unsigned char*, D_800F58BC);
    if (*(unsigned short*)(base + 0x8e))
        return 1;
    unsigned members = room[0x12];
    if (room[0xd] < members)
        return 0;
    if (*(unsigned short*)(base + 0x98) | D_800F58E0)
        return 0;
    base[0x12c] = 0;
    *(unsigned short*)(room + 0x10) |= 1u << base[0x88];
    room[0x12] = members + 1;
    *(unsigned short*)(base + 0x8e) = *(unsigned short*)(base + 0x9a) ? 0x20 : 1;
    unsigned head = *(unsigned*)(room + 0x1c);
    if (!head)
        psx_store(room + 0x20, base);
    *(unsigned*)(base + 0x1a0) = head;
    psx_store(room + 0x1c, base);
    return 1;
}
/* 6FA6C.s: leave the encounter: clear the mask bit and timer, block
 * re-joining (0x98 = 0x30 unless exactly two actors remain; 0x9A = 90),
 * unlink (func_800DBF00), drop a member and notify (func_800D820C). */
void func_800D82CC(void* state)
{
    unsigned char* base = state;
    unsigned char* room = psx_to_ptr(unsigned char*, D_800F58BC);
    int id = base[0x88];
    unsigned mask = *(unsigned short*)(room + 0x10), bit = 1u << id;
    if (!(mask & bit))
        return;
    *(unsigned short*)(room + 0x10) = mask - bit;
    *(unsigned short*)(base + 0x8e) = 0;
    unsigned char* second = psx_load(unsigned char*, vs_battle_actors[0]);
    *(unsigned short*)(base + 0x98) = second && !psx_load(unsigned char*, second) ? 0 : 0x30;
    *(unsigned short*)(base + 0x9a) = 90;
    func_800DBF00(state);
    room[0x12] -= 1;
    func_800D820C(id, 0);
}
/* 6FA6C.s: func_800D82CC for actor id (nonzero, present). */
void func_800D82A8(int id)
{
    if (id && D_800F5878[id])
        func_800D82CC(D_800F5878[id]);
}

/* 7C7B0.s: whether actor `id` is farther than the reach threshold. The
 * original adds 0x80 to `range` for model class 10 but then squares its
 * constant 10 instead, so the test is always 100 < distance (unsigned). */
int func_800E4FB0(void* state, int id, int range)
{
    (void)range;
    return 100u < *(unsigned*)((unsigned char*)state + 0x1e4 + id * 4);
}

/* 7B5DC.s floor-height helpers over the grid cached at scratchpad 0x3BC
 * (64-byte rows of 16-bit cells): raw height sign((cell & 0x3FF) << 6) minus
 * (cell & 0x1C00) >> 3; the *EC variants sign-extend it from 15 bits. */
int func_800E4320(int x, int z)
{
    unsigned char* grid = psx_load(unsigned char*, 0x1f800000 + 0x3bc);
    unsigned cell = *(unsigned short*)(grid + z * 64 + x * 2);
    return ((int)(cell << 22) >> 16) - (int)((cell & 0x1c00) >> 3);
}
int func_800E4318(int x, int z) { return func_800E4320(x >> 7, z >> 7); }
int func_800E4308(unsigned tile) { return func_800E4320(tile & 0xff, (tile >> 16) & 0xff); }
int func_800E42EC(int x, int z) { return (int)((unsigned)func_800E4320(x, z) << 17) >> 17; }
int func_800E42E0(unsigned tile) { return func_800E42EC(tile & 0xff, (tile >> 16) & 0xff); }
int func_800E42D4(int x, int z) { return func_800E42EC(x >> 7, z >> 7); }

/* Scratchpad tables set up by the movement code (see func_800E65DC):
 * 0x3EC + dir: packed direction (dx signed bits 0-1, dz signed bits 4-5);
 * 0x3E0 + 3x3 index: per-neighbour direction or -1; 0x3C0: navigation cells;
 * 0x3BC: floor cells; 0x3C4/0x3DC/0x3F4: optional occupancy bitmap.
 * SCRATCH8/SCRATCH32/SCRATCH_PTR are defined at the top of the file. */
static int direction_dx(int packed) { return (int)((unsigned)packed << 30) >> 30; }
static int direction_dz(int packed) { return (int)((unsigned)packed << 26) >> 30; }

/* 7BDD4.s: whether stepping from `tile` in direction `dir` is blocked: the
 * neighbour has no floor (raw height negative) or differs by 64 or more. */
int func_800E4690(unsigned tile, int dir)
{
    int x = tile & 0xff, z = (tile >> 16) & 0xff;
    int here = (int)((unsigned)func_800E4320(x, z) << 17) >> 17;
    int packed = (signed char)SCRATCH8(0x3ec + dir);
    int raw = func_800E4320(x + direction_dx(packed), z + direction_dz(packed));
    if (raw < 0)
        return 1;
    int difference = here - ((int)((unsigned)raw << 17) >> 17);
    if (difference < 0)
        difference = -difference;
    return (difference >> 6) != 0;
}

/* 70D4C.s: id + 1 of the first other actor whose bounds overlap the point one
 * radius ahead of `state` in direction `dir`, else 0; cached per direction in
 * scratchpad 0x3D2 (valid bits) / 0x3D4 + dir. */
int func_800D98E8(void* state, int dir)
{
    unsigned char* base = state;
    if ((SCRATCH8(0x3d2) >> dir) & 1)
        return SCRATCH8(0x3d4 + dir);
    int packed = SCRATCH8(0x3ec + dir), dx = direction_dx(packed), dz = direction_dz(packed);
    int radius = base[0xb3];
    unsigned x = *(unsigned short*)(base + 0x4c) + radius * dx;
    unsigned z = *(unsigned short*)(base + 0x50) + radius * dz;
    unsigned tile = *(unsigned*)(base + 0x34);
    int tile_x = tile & 0xff, tile_z = (tile >> 16) & 0xff;
    int neighbour = (int)(((z >> 7) - tile_z + 1) * 4 + ((x >> 7) - tile_x + 1));
    int lookup = (signed char)SCRATCH8(0x3e0 + neighbour), open = 1;
    if (lookup >= 0) {
        if (SCRATCH32(0x3f4)) {
            unsigned char* bitmap = SCRATCH_PTR(unsigned char*, 0x3c4);
            open = (bitmap[SCRATCH8(0x3dc) * tile_z + tile_x] >> lookup) & 1;
        } else {
            open = func_800E4690(tile, lookup);
        }
    }
    int height = open ? *(short*)(base + 0x4e) : func_800E42D4((int)x, (int)z);
    unsigned self_size = *(unsigned short*)(base + 0x94), self_height = *(unsigned short*)(base + 0xb8);
    int result = 0;
    for (unsigned char* node = vs_battle_actors[0]; node; node = psx_load(unsigned char*, node)) {
        int id = *(int*)(node + 4);
        if (id == base[0x88])
            continue;
        unsigned char* model = D_800F4538[id];
        unsigned char* other = SCRATCH_PTR(unsigned char*, 0x37c + id * 4);
        unsigned position, depth;
        if (id && (model[0xa] & 0x1f)) {
            position = *(unsigned*)(other + 0x174);
            depth = *(unsigned short*)(other + 0x178);
        } else {
            position = *(unsigned*)(model + 0x1c);
            depth = *(unsigned short*)(model + 0x20);
        }
        int ox = (int)(position & 0xffff) - (int)x, oy = ((int)position >> 16) - height;
        int oz = (int)depth - (int)z;
        if ((dx > 0 && ox < 0) || (dx < 0 && ox > 0) || (dz > 0 && oz < 0) || (dz < 0 && oz > 0))
            continue;
        if (ox <= 0)
            ox = -ox;
        if (oy <= 0)
            oy = -oy;
        if (oz <= 0)
            oz = -oz;
        int reach = *(unsigned short*)(other + 0x94) + other[0xb3] + self_size;
        if (ox - reach > 0 || oz - reach > 0)
            continue;
        if (oy - (int)((self_height + *(unsigned short*)(other + 0xb8)) >> 1) > 0)
            continue;
        result = id + 1;
        break;
    }
    SCRATCH8(0x3d4 + dir) = result;
    SCRATCH8(0x3d2) |= 1u << dir;
    return result;
}

/* 70D4C.s D_800D9B6C: whether direction `dir` is passable (1) or blocked (0),
 * cached in scratchpad 0x3D0 (valid bits) / 0x3D1 (passable bits). Blocked by
 * an actor (func_800D98E8), the occupancy/step test, reserved cell 0x38, cell
 * flags (bit 4 unless state byte 6, bits 5/7, mask byte 0x32 against bits
 * 8-15), a different area code when required, or a higher floor level. The
 * original also stashes `dir` in GTE VXY2, which is not reproduced. */
int D_800D9B6C(void* state, int dir)
{
    unsigned char* base = state;
    if ((SCRATCH8(0x3d0) >> dir) & 1)
        return (SCRATCH8(0x3d1) >> dir) & 1;
    int blocked = func_800D98E8(state, dir);
    if (!blocked) {
        unsigned tile = *(unsigned*)(base + 0x34), fine = *(unsigned*)(base + 0x120);
        int packed = SCRATCH8(0x3ec + dir), probe = dir;
        int tile_x = tile & 0xff, tile_z = (tile >> 16) & 0xff;
        int next_x = ((int)(fine & 0xff) + direction_dx(packed)) >> 1;
        int next_z = ((int)((fine >> 16) & 0xff) + direction_dz(packed)) >> 1;
        if (next_x == tile_x && next_z == tile_z)
            goto done;
        if (dir & 1)
            probe = (signed char)SCRATCH8(0x3e0 + (next_x - tile_x + 1) + (next_z - tile_z + 1) * 4);
        unsigned char* cells = SCRATCH_PTR(unsigned char*, 0x3c0);
        unsigned short* cell = (unsigned short*)(cells + next_x * 2 + next_z * 64);
        if (!SCRATCH32(0x3f4)) {
            blocked = func_800E4690(tile, probe);
        } else {
            unsigned reserved = *(unsigned*)(base + 0x38);
            unsigned char* reserved_cell =
                cells + (((reserved >> 10) & 0x3fc0) | ((reserved & 0xff) << 1));
            if (reserved_cell == (unsigned char*)cell) {
                blocked = 1;
                goto done;
            }
            unsigned char* bitmap = SCRATCH_PTR(unsigned char*, 0x3c4);
            blocked = (bitmap[tile_z * SCRATCH8(0x3dc) + tile_x] >> probe) & 1;
        }
        if (blocked)
            goto done;
        unsigned flags = *cell;
        if ((blocked = (flags >> 4) & (base[6] ^ 1)))
            goto done;
        if ((blocked = ((flags >> 5) & 1) | ((flags >> 7) & 1)))
            goto done;
        if ((blocked = base[0x32] & (flags >> 8)))
            goto done;
        unsigned here = *(unsigned short*)(cells + tile_x * 2 + tile_z * 64);
        if ((base[0x29] & (base[2] ^ 1)) && (here & 0xf) != (flags & 0xf)
            && (base[0x154] & 0xf) != (flags & 0xf)) {
            blocked = 1;
            goto done;
        }
        unsigned char* floors = SCRATCH_PTR(unsigned char*, 0x3bc);
        blocked = (int)(*(unsigned short*)(floors + tile_x * 2 + tile_z * 64) & 0x1c00)
                < (int)(*(unsigned short*)(floors + next_x * 2 + next_z * 64) & 0x1c00);
    }
done:
    SCRATCH8(0x3d0) |= 1u << dir;
    if (blocked)
        SCRATCH8(0x3d1) &= ~(1u << dir);
    else
        SCRATCH8(0x3d1) |= 1u << dir;
    return (SCRATCH8(0x3d1) >> dir) & 1;
}

/* 7BDD4.s: scratchpad linear-congruential step (seed half 0x3F8 + increment
 * word 0x3FC, stored back as a word); returns the value & (n - 1). */
int func_800E45D4(int n)
{
    int value = *(short*)(0x1f800000 + 0x3f8) + (int)SCRATCH32(0x3fc);
    SCRATCH32(0x3f8) = value;
    return value & (n - 1);
}

static unsigned short* nav_cell(int x, int z)
{
    return (unsigned short*)(SCRATCH_PTR(unsigned char*, 0x3c0) + z * 64 + x * 2);
}

/* 70D4C.s: whether `tile` can be entered: 0 for the reserved tile (0x38,
 * compared on its low 24 bits against tile with byte 1 cleared), outside the
 * grid, or blocked cell flags (0x80B0 plus the actor's mask 0x32 in bits
 * 8-15); -1 when another actor's bounds overlap the tile centre; else 1. */
int func_800D954C(void* state, unsigned tile)
{
    unsigned char* base = state;
    if ((*(unsigned*)(base + 0x38) << 8) == ((tile & 0xffff00ff) << 8))
        return 0;
    int x = tile & 0xff, z = (tile >> 16) & 0xff;
    int height = func_800E42EC(x, z);
    int point_x = x * 128 + 64, point_z = z * 128 + 64;
    if (!(x < SCRATCH8(0x3dc) && z < SCRATCH8(0x3dd)))
        return 0;
    if (*nav_cell(x, z) & ((base[0x32] << 8) | 0x80b0))
        return 0;
    unsigned self_size = *(unsigned short*)(base + 0x94);
    for (unsigned char* node = vs_battle_actors[0]; node; node = psx_load(unsigned char*, node)) {
        int id = *(int*)(node + 4);
        if (id == base[0x88])
            continue;
        unsigned char* model = D_800F4538[id];
        /* Actors without an AI state (no attribute bit 0/2, func_800E6178)
         * have a null entry: the original then reads offsets 0x94/0x174/
         * 0x178 of address 0, the start of PS1 RAM (kernel area, seen
         * through KUSEG). Read the same guest RAM bytes. */
        unsigned char* other = SCRATCH_PTR(unsigned char*, 0x37c + id * 4);
        if (!other)
            other = psx_to_ptr(unsigned char*, 0x80000000u);
        unsigned position, depth;
        if (id && (model[0xa] & 0x1f)) {
            position = *(unsigned*)(other + 0x174);
            depth = *(unsigned short*)(other + 0x178);
        } else {
            position = *(unsigned*)(model + 0x1c);
            depth = *(unsigned short*)(model + 0x20);
        }
        int dx = (int)(position & 0xffff) - point_x, dy = ((int)position >> 16) - height;
        int dz = (int)depth - point_z;
        int reach = *(unsigned short*)(other + 0x94) + self_size;
        if (dx < 0)
            dx = -dx;
        if (dy < 0)
            dy = -dy;
        if (dz < 0)
            dz = -dz;
        if (!(reach < dx) && !(0x80 < dy) && !(reach < dz))
            return -1;
    }
    return 1;
}

/* 70D4C.s: whether the actor may step from tile `from` to tile `to` in
 * direction `dir`: 1 yes, 0 no, -1 when the step leaves its approach radius.
 * Quirks kept: for a negative X step the original negates the Z delta instead
 * of X; the area-code test always uses byte 0x132 (0x154 is loaded then
 * overwritten); the approach test reads halfwords from the D_800F4538 array
 * itself (+0x1C/+0x20 past the entry, not through it) and passes the packed
 * `to` tile as the SQR Z input. */
int func_800D96C8(void* state, unsigned from, unsigned to, int dir)
{
    unsigned char* base = state;
    if (base[7])
        return 0;
    int x = to & 0xff, z = (to >> 16) & 0xff;
    if (!(x < SCRATCH8(0x3dc) && z < SCRATCH8(0x3dd)))
        return 0;
    unsigned cell = *nav_cell(x, z);
    if (cell & 0x30)
        return 0;
    if (!base[2] && (cell & 0xf) != base[0x132])
        return 0;
    int from_x = from & 0xff, from_z = (from >> 16) & 0xff;
    int step_x = x - from_x, step_z = z - from_z;
    int rise = func_800E42EC(x, z) - func_800E42EC(from_x, from_z);
    if (step_x < 0)
        step_z = -step_z;
    if (step_z < 0)
        step_z = -step_z;
    int climbing = base[3];
    int checked = 0;
    if (step_x < 2 && step_z < 2) {
        if (func_800D98E8(state, dir))
            return 0;
        checked = !(climbing | (rise < 1));
    }
    if (!checked) {
        if (base[0xb2] < step_x + step_z)
            return 0;
        int limit = (base[0x15e] & 0xf8) ? 0x2000 : *(unsigned short*)(base + 0xb0);
        if ((limit < rise) & climbing)
            return 0;
        if (rise < -limit)
            return 0;
    }
    if (!base[5])
        return 1;
    if (base[0x89] == 1) {
        unsigned char* entry = (unsigned char*)D_800F4538 + base[0xe5] * 4;
        int radius = *(int*)(base + 0xdc) - 0x60;
        int distance = func_800E4660(x * 128 + 64 - *(unsigned short*)(entry + 0x1c),
            z * 128 + 64 - *(unsigned short*)(entry + 0x20), (int)to);
        if (distance < (int)((unsigned)radius * radius))
            return -1;
    }
    unsigned target = *(unsigned*)(base + 0x168);
    int current = func_800E45F4(*(unsigned*)(base + 0x34), target);
    int next = func_800E4660(x - (int)(target & 0xff), 0, z - (int)((target >> 16) & 0xff));
    return next < current;
}

typedef int (*direction_test)(void*, int);
extern int rcos(int);
extern int rsin(int);
extern void func_800DEEFC(void*, int);
extern unsigned char D_800F1758[];

/* 7B55C.s: passability bits of the four axis directions (0, 2, 4, 6) as
 * bits 3..0. The original stashes its state in GTE registers (IR0-IR3,
 * SXY0/1, MAC1), which is not reproduced. */
int func_800E3D5C(void* state, direction_test test)
{
    int a = test(state, 0), b = test(state, 2), c = test(state, 4), d = test(state, 6);
    return (a << 3) | (b << 2) | (c << 1) | d;
}

/* 7B3C8.s: record the step tile toward the target (0x168) in the path flags
 * (0x190 bits 22+ = x, 27+ = z; 0 per axis already reached, otherwise the
 * target coordinate plus one when it lies ahead). The fields overlap, as in the
 * original. */
void func_800E3BC8(void* state)
{
    unsigned char* base = state;
    unsigned target = *(unsigned*)(base + 0x168), tile = *(unsigned*)(base + 0x34);
    int target_x = target & 0xff, target_z = (target >> 16) & 0xff;
    int tile_x = tile & 0xff, tile_z = (tile >> 16) & 0xff;
    unsigned x = target_x != tile_x ? target_x + (tile_x < target_x) : 0;
    unsigned z = target_z != tile_z ? target_z + (tile_z < target_z) : 0;
    *(unsigned*)(base + 0x190) = (*(unsigned*)(base + 0x190) & 0x3fffff) | (x << 22) | (z << 27);
}

/* 7B3C8.s: forget (0x194/0x198/0x19A) a tile the actor keeps revisiting
 * (5 frames running blocks it for 120), clear 0x156, and store the step
 * count (0x7E, or 0x1F at random for non-flagged actors) in flags bits 5-11. */
unsigned func_800E3CDC(void* state, unsigned flags)
{
    unsigned char* base = state;
    unsigned tile = *(unsigned*)(base + 0x34);
    int repeats = (*(unsigned*)(base + 0x194) & 0xff00ff) == (tile & 0xff00ff) ? base[0x198] + 1 : 0;
    if (repeats >= 5) {
        base[0x19a] = 120;
        repeats = 0;
    }
    *(unsigned*)(base + 0x194) = tile;
    base[0x198] = repeats;
    *(unsigned short*)(base + 0x156) = 0;
    unsigned steps = 0x7e;
    if (!base[0x23] && !func_800E45D4(4))
        steps = 0x7e >> 2;
    return (flags & 0xfffff01f) | (steps << 5);
}

/* 7B5DC.s: start a step plan. When the plan was empty, forced (bit 16) or the
 * actor is fleeing (byte 4), pick a random turn side (bit 4) and maybe shout
 * (func_800E4180 kind 1); clear bit 17 when no steps remain; store the
 * passability of the four axis directions in bits 18-21. */
unsigned func_800E3DDC(void* state, unsigned flags, direction_test test)
{
    unsigned char* base = state;
    base[0x199] = base[1];
    if (!(flags & 0xfe0) || ((flags >> 16) & 1) || base[4]) {
        flags &= ~0x10000u;
        flags = (flags & ~0x10u) | ((unsigned)func_800E45D4(2) << 4);
        func_800E4180(state, 8, 1, base[0x89]);
    }
    if (!(flags & 0xfffff01f))
        flags &= ~0x20000u;
    return (flags & 0xffc30fff) | ((unsigned)func_800E3D5C(state, test) << 18);
}

/* Shared tail of the three step planners (7B5DC.s L800E3F50/L800E4084). */
static int finish_step(unsigned char* base, unsigned flags, int dir, int axis_only)
{
    unsigned plan = func_800E3CDC(base, (flags & 0xfffffff1) | ((unsigned)dir << 1));
    unsigned tile = *(unsigned*)(base + 0x34), target = *(unsigned*)(base + 0x168);
    int tile_x = tile & 0xff, tile_z = (tile >> 16) & 0xff;
    int target_x = target & 0xff, target_z = (target >> 16) & 0xff;
    unsigned x, z;
    if (!axis_only) {
        z = tile_z == target_z ? 0 : tile_z + (tile_z < target_z);
        x = tile_x == target_x ? 0 : tile_x + (tile_x < target_x);
    } else if (plan & 4) {
        z = tile_z + (tile_z < target_z);
        x = 0;
    } else {
        x = tile_x + (tile_x < target_x);
        z = 0;
    }
    plan = (plan & 0x3fffff) | (z << 27) | (x << 22) | 1;
    *(unsigned*)(base + 0x190) = plan;
    return 1;
}
/* 7B5DC.s: step around an obstacle between headings `from` and `to`. */
int func_800E3EAC(void* state, int from, int to, direction_test test)
{
    unsigned char* base = state;
    unsigned flags = func_800E3DDC(state, *(unsigned*)(base + 0x190), test);
    int turn = (flags & 0x10) ? 2 : -2;
    int offset = ((from + turn) & 7) == to ? turn : 4;
    int dir = (to + offset) & 7;
    if (!test(state, dir)) {
        dir = (((flags >> 1) & 7) + turn) & 7;
        if (!test(state, dir)) {
            *(unsigned*)(base + 0x190) = flags;
            return 0;
        }
    }
    return finish_step(base, flags, dir, 0);
}
static int final_step(unsigned char* base, unsigned flags, int dir, direction_test test)
{
    if (!test(base, dir)) {
        *(unsigned*)(base + 0x190) = flags;
        return 0;
    }
    return finish_step(base, flags, dir, 1);
}
/* 7B5DC.s: keep following a wall: `side` is +-1; turning the planned way
 * succeeds at once, otherwise try the reverse heading, then reverse - turn. */
int func_800E3FF4(void* state, int heading, int side, direction_test test)
{
    unsigned char* base = state;
    unsigned flags = func_800E3DDC(state, *(unsigned*)(base + 0x190), test);
    int turn = (flags & 0x10) ? 1 : -1;
    if (turn == side)
        return finish_step(base, flags, heading, 1);
    int dir = (heading + 4) & 7;
    if (test(state, dir))
        return finish_step(base, flags, dir, 1);
    return final_step(base, flags, (dir - turn * 2) & 7, test);
}
/* 7B5DC.s (0x800E40F0): step toward heading `dir`, trying dir - turn, its
 * reverse, then dir reversed. */
int func_800E40F0(void* state, int dir, direction_test test)
{
    unsigned char* base = state;
    unsigned flags = func_800E3DDC(state, *(unsigned*)(base + 0x190), test);
    int turn = (flags & 0x10) ? 2 : -2;
    int next = (dir - turn) & 7;
    if (test(state, next))
        return finish_step(base, flags, next, 1);
    next = (next + 4) & 7;
    if (test(state, next))
        return finish_step(base, flags, next, 1);
    return final_step(base, flags, (dir + 4) & 7, test);
}

/* 7B3C8.s entries 0x800E3C30/3C3C/3C48: run a step planner only when the
 * target is within 0x31000 (squared, tile centres) and byte 4 bit 0 is set,
 * then refresh the step tile (func_800E3BC8). The original stashes $ra/$a0/$v0
 * in GTE VXY0/VZ0/VXY1, which is not reproduced. */
static int near_target(unsigned char* base)
{
    unsigned tile = *(unsigned*)(base + 0x34), target = *(unsigned*)(base + 0x168);
    int dx = (int)((target & 0xff) << 7) - (int)((tile & 0xff) << 7);
    int dz = (int)(((target << 8) >> 17)) - (int)(((tile << 8) >> 17));
    return ((dx * dx + dz * dz) < 0x31000) & *(int*)(base + 4);
}
int func_800E3C30(void* state, int a, int b, direction_test test)
{
    if (!near_target(state))
        return 0;
    int result = func_800E3EAC(state, a, b, test);
    func_800E3BC8(state);
    return result;
}
int func_800E3C3C(void* state, int a, int b, direction_test test)
{
    if (!near_target(state))
        return 0;
    int result = func_800E3FF4(state, a, b, test);
    func_800E3BC8(state);
    return result;
}
int func_800E3C48(void* state, int a, direction_test test)
{
    if (!near_target(state))
        return 0;
    int result = func_800E40F0(state, a, test);
    func_800E3BC8(state);
    return result;
}
/* Native D_800F1760 (rows: around, follow, toward; columns: approach mode).
 * build_boot.py verifies the original words. */
int (*D_800F1760[3][4])() = {
    { (int (*)())func_800E3EAC, (int (*)())func_800E3C30, (int (*)())func_800E3EAC,
        (int (*)())func_800E3C30 },
    { (int (*)())func_800E3FF4, (int (*)())func_800E3C3C, (int (*)())func_800E3FF4,
        (int (*)())func_800E3C3C },
    { (int (*)())func_800E40F0, (int (*)())func_800E3C48, (int (*)())func_800E40F0,
        (int (*)())func_800E3C48 },
};

/* 7B5DC.s: per-slot cry/animation request (0x474 + slot * 2: count << 8 |
 * kind). A new request (kind != 3) fires with chance/128 and loads its length
 * from D_800F1758; each call counts down, sets 0x13A = 0x40 and, for kinds 1
 * and 2, a pitch at 0x188 by remaining length before func_800DEEFC(16).
 * Returns 1 while at least 2 frames remained. */
int func_800E4180(void* state, int chance, int kind, int slot)
{
    unsigned char* base = state;
    unsigned short* entry = (unsigned short*)(base + 0x474 + slot * 2);
    int count = (*entry >> 8) & 0xff, stored = *entry & 0xff;
    if (!count) {
        if (kind == 3)
            return 0;
        if (chance < func_800E45D4(0x80))
            return 0;
        count = D_800F1758[kind];
        stored = kind;
    }
    *entry = ((count - 1) << 8) | stored;
    *(unsigned short*)(base + 0x13a) = 0x40;
    int pitch;
    if (kind == 2)
        pitch = count < 0xb ? 0 : 0x500;
    else if (kind == 1)
        pitch = count >= 0x35 ? 0x320 : count >= 0x12 ? -0x320 : 0;
    else
        return 0;
    *(short*)(base + 0x188) = pitch;
    func_800DEEFC(state, 16);
    return count >= 2;
}

/* 7BDD4.s: whether `tile` lies behind the actor relative to its facing
 * target (actor 0x15C, or its own model heading when that is >= 16). */
int func_800E47E4(void* state, unsigned tile)
{
    unsigned char* base = state;
    if (base[0xc])
        return 0;
    unsigned current = *(unsigned*)(base + 0x34);
    int dx = (int)(tile & 0xff) - (int)(current & 0xff);
    int dz = (int)((tile >> 16) & 0xff) - (int)((current >> 16) & 0xff);
    int facing = base[0x15c], angle;
    if (facing >= 16) {
        angle = *(unsigned short*)(D_800F4538[base[0x88]] + 0x26);
    } else {
        unsigned char* model = D_800F4538[facing];
        angle = ratan2(*(unsigned short*)(base + 0x4c) - *(unsigned short*)(model + 0x1c),
            *(unsigned short*)(base + 0x50) - *(unsigned short*)(model + 0x20));
    }
    int turned = 0xc00 - angle;
    int along = rcos(turned) * dx;
    return rsin(turned) * dz + along < 0;
}

/* 7BDD4.s: try climbing toward a higher tile: from `tile` walk up to
 * steps - 1 tiles in `dir` while the floor stays at least as high; on the
 * first lower tile return it if func_800D96C8 and func_800D954C accept the
 * step, else 0. Only with the occupancy bitmap active (0x3F4). The original
 * intended min(steps - 1, 2) but tests its constant, so the bound is steps - 1. */
unsigned func_800E49D8(void* state, unsigned tile, int dir, int steps)
{
    unsigned char* base = state;
    if (!SCRATCH32(0x3f4))
        return 0;
    unsigned current = *(unsigned*)(base + 0x34);
    int floor = func_800E42E0(current);
    int raw = func_800E4308(tile);
    if (raw < 0)
        return 0;
    int target = (int)((unsigned)raw << 17) >> 17;
    if (!(floor < target) || !steps)
        return 0;
    floor = target;
    int limit = steps - 1, taken = 0;
    int packed = SCRATCH8(0x3ec + dir);
    int x = tile & 0xff, z = (tile >> 16) & 0xff;
    for (;;) {
        x += direction_dx(packed);
        z += direction_dz(packed);
        int height = func_800E42EC(x, z);
        if (height < floor)
            break;
        if (!(++taken < limit))
            return 0;
    }
    tile = (tile & 0xff00ff00) | (unsigned)x | ((unsigned)z << 16);
    if (func_800D96C8(state, current, tile, dir) <= 0 || func_800D954C(state, tile) <= 0)
        return 0;
    return tile;
}

/* 7BDD4.s: mark nav cell (x, z) of D_800F58B8 occupied (bit 0x80). */
void func_800E4C1C(int x, int z)
{
    if (D_800F58B8)
        *psx_to_ptr(unsigned short*, D_800F58B8 + x * 2 + z * 64) |= 0x80;
}

/* func_800DEEFC speed selection from the actor's gait word 0x224: running
 * (byte 1) uses mask `run` (bytes 0/1 test, speed from byte 2/3), walking
 * uses `walk_low`/`walk_high`. Returns 0 with *command set, or -1 to stand. */
static int gait_speed(unsigned char* base, unsigned walk_high, unsigned* speed, int* command)
{
    unsigned gait = *(unsigned*)(base + 0x224);
    if (base[1]) {
        if (gait & 0x84) {
            *speed = (gait >> 16) & 0xff;
            *command = 10;
            return 0;
        }
        if (gait & 0x8400) {
            *speed = (gait >> 24) & 0xff;
            *command = 10;
            return 0;
        }
    }
    if (gait & 0x42) {
        *speed = (gait >> 16) & 0xff;
        *command = 9;
        return 0;
    }
    if (gait & walk_high) {
        *speed = (gait >> 24) & 0xff;
        *command = 9;
        return 0;
    }
    return -1;
}

/* func_800DEEFC's shared move (L800DEF8C): face `angle`, pick the gait and
 * step; if a corner of the actor's square (half-size 0x92) would enter a
 * blocked neighbour, slide to the tile edge instead (command 0x0D). */
static void move_toward(unsigned char* base, unsigned char* command, int angle)
{
    int size = *(unsigned short*)(base + 0x92);
    unsigned tile = *(unsigned*)(base + 0x34);
    int tile_x = tile & 0xff, tile_z = (tile >> 16) & 0xff;
    angle &= 0xfff;
    *(unsigned short*)(command + 0xc) = angle;
    int turned = (0xc00 - angle) & 0xfff;
    int x = *(unsigned short*)(base + 0x4c), z;
    unsigned speed;
    int kind;
    if (gait_speed(base, 0x420000, &speed, &kind) < 0) {
        command[9] = 2;
        return;
    }
    command[9] = kind;
    int step_x = rcos(turned) * (int)speed, step_z = rsin(turned) * (int)speed;
    z = *(unsigned short*)(base + 0x50);
    int new_x = x + (step_x >> 12), new_z = z + (step_z >> 12);
    unsigned moved = (unsigned)new_x | ((unsigned)new_z << 16);
    int lo_x = ((new_x - size) >> 7) - tile_x + 1, lo_z = ((new_z - size) >> 7) - tile_z + 1;
    int hi_x = ((new_x + size) >> 7) - tile_x + 1, hi_z = ((new_z + size) >> 7) - tile_z + 1;
    int corners[4][2] = { { lo_x, lo_z }, { lo_x, hi_z }, { hi_x, lo_z }, { hi_x, hi_z } };
    int blocked = 0;
    for (int i = 0; i < 4 && !blocked; ++i) {
        int dir = (signed char)SCRATCH8(0x3e0 + corners[i][1] * 4 + corners[i][0]);
        if (dir < 0)
            continue;
        if (SCRATCH32(0x3f4)) {
            unsigned char* floors = SCRATCH_PTR(unsigned char*, 0x3bc);
            unsigned char* bitmap = SCRATCH_PTR(unsigned char*, 0x3c4);
            int here = (*(unsigned short*)(floors + tile_z * 64 + tile_x * 2) >> 10) & 7;
            int there = (*(unsigned short*)(floors + (tile_z + corners[i][1] - 1) * 64
                             + (tile_x + corners[i][0] - 1) * 2) >> 10) & 7;
            blocked = ((bitmap[SCRATCH8(0x3dc) * tile_z + tile_x] >> dir) & 1) | (here < there);
        } else {
            blocked = func_800E4690(tile, dir);
        }
    }
    if (!blocked)
        return;
    int centre_x = tile_x * 128 + 64, centre_z = tile_z * 128 + 64;
    int slide_x = ((int)(moved & 0xffff) < centre_x ? centre_x - 0x40 + size : centre_x + 0x3f - size) - x;
    int slide_z = ((int)(moved >> 16) < centre_z ? centre_z - 0x40 + size : centre_z + 0x3f - size) - z;
    if (!(slide_x | slide_z))
        return;
    unsigned packed = ((unsigned)tile_z << 16) | (unsigned)tile_x;
    if ((*(unsigned*)(base + 0x9c) & 0xff00ff) == packed)
        return;
    *(unsigned*)(base + 0x9c) = packed;
    command[9] = 0xd;
    *(int*)(command + 0xc) = slide_x << 12;
    *(int*)(command + 0x10) = slide_z << 12;
}

/* func_800DEEFC entry 13 (L800DF4EC): walk to the stored tile 0x170. */
static void walk_to_tile(unsigned char* base, unsigned char* command)
{
    unsigned tile = *(unsigned*)(base + 0x170);
    command[9] = 0x17;
    *(unsigned*)(command + 0xc) = tile;
    int x = (tile & 0xff) * 128 + 64, z = (int)((tile << 8) >> 17) + 64;
    *(unsigned short*)(base + 0x178) = z;
    *(unsigned*)(base + 0x174) = ((unsigned)func_800E42D4(x, z) << 16) | (unsigned)x;
    int heading = ratan2(*(unsigned short*)(base + 0x4c) - x, *(unsigned short*)(base + 0x50) - z);
    *(int*)(command + 0x10) = heading;
    func_800E4C1C(tile & 0xff, (tile >> 16) & 0xff);
}

/* 766FC.s: issue movement/action command `action` to the actor's command
 * block (0x54): record it at scratchpad 0x3C8, reset the block (byte 8 =
 * 0x80, byte 10 = facing target) and fill it through jtbl_800F16F8 (18
 * entries). Byte 9 receives the command code. */
void func_800DEEFC(void* state, int action)
{
    unsigned char* base = state;
    unsigned char* command = psx_load(unsigned char*, base + 0x54);
    SCRATCH32(0x3c8) = action;
    command[8] = 0x80;
    command[0xa] = (signed char)base[0x15c];
    switch (action) {
    case 0:
        command[9] = 2;
        return;
    case 1:
        command[9] = 1;
        return;
    case 2:
        command[9] = 0x14;
        return;
    case 3:
        command[9] = 3;
        *(unsigned*)(command + 0xc) = ((unsigned)*(unsigned short*)(base + 0xba) << 16)
            | (*(unsigned short*)(base + 0x188) & 0xfff);
        return;
    case 4: {
        unsigned char* model = psx_load(unsigned char*, base + 0x58);
        int target = *(unsigned short*)(base + 0x18a) & 0xfff;
        int heading = *(short*)(model + 0x26), turn = *(short*)(model + 0x14);
        int offset = (target - (turn + heading)) & 0xfff, sign, limit_ok;
        if (offset < 0x800) {
            limit_ok = turn < 0x320;
            sign = 1;
        } else {
            limit_ok = -0x320 < turn;
            sign = -1;
        }
        int code = (base[0x2a] & limit_ok) ? 7 : 3;
        unsigned rate = *(unsigned short*)(base + 0x13a);
        command[9] = code;
        int value;
        if ((int)(0x1000 - offset) < (int)rate || offset < (int)rate || offset < 1) {
            command[9] = 4;
            value = target;
        } else {
            value = sign * (int)rate + (code == 3 ? heading : turn);
        }
        *(unsigned*)(command + 0xc) = ((unsigned)value & 0xffff) | (rate << 16);
        return;
    }
    case 5:
        move_toward(base, command, *(short*)(base + 0x188) << 9);
        return;
    case 6: {
        unsigned tile = *(unsigned*)(base + 0x34), target = *(unsigned*)(base + 0x168);
        move_toward(base, command,
            ratan2((int)(tile & 0xff) - (int)(target & 0xff),
                (int)(((tile >> 8) << 24) - ((target >> 8) << 24))));
        return;
    }
    case 7:
        move_toward(base, command, *(short*)(base + 0x188));
        return;
    case 8:
        command[9] = 0x13;
        *(unsigned*)(command + 0xc) = *(unsigned*)(base + 0x17c);
        *(unsigned*)(command + 0x14) = *(unsigned*)(base + 0x184);
        *(unsigned*)(command + 0x10) = *(unsigned*)(base + 0x180);
        return;
    case 9:
    case 10:
        command[9] = action == 9 ? 0x10 : 0x11;
        *(unsigned*)(command + 0xc) =
            (*(unsigned*)(base + 0x184) << 16) | (*(unsigned*)(base + 0x17c) & 0xffff);
        *(unsigned short*)(command + 0x10) = *(unsigned*)(base + 0x180);
        return;
    case 11: {
        unsigned tile = *(unsigned*)(base + 0x170);
        *(unsigned*)(command + 0xc) = tile;
        command[9] = 0x15;
        func_800E4C1C(tile & 0xff, (tile >> 16) & 0xff);
        return;
    }
    case 12: {
        unsigned tile = *(unsigned*)(base + 0x170);
        int lift = *(short*)(base + 0xbe);
        command[9] = 0x16;
        int x = (int)(tile & 0xff) << 7, z = (int)((tile << 8) >> 17) + 0x40;
        *(unsigned short*)(command + 0x10) = z;
        int height = func_800E42D4(x, z);
        *(unsigned*)(command + 0xc) = ((unsigned)(height + lift) << 16) | (unsigned)(x + 0x40);
        return;
    }
    case 13:
        walk_to_tile(base, command);
        return;
    case 14: {
        int here = func_800E42E0(*(unsigned*)(base + 0x34));
        unsigned tile = *(unsigned*)(base + 0x170);
        int rise = func_800E42E0(tile) - here;
        unsigned flags = *(unsigned short*)(base + 0x15e);
        int code;
        if (rise < 0)
            rise = -rise;
        if (rise < *(unsigned short*)(base + 0xb0))
            code = 0xb;
        else if (flags & 8) {
            walk_to_tile(base, command);
            return;
        } else if (flags & 0x30)
            code = 0x12;
        else if (flags & 0xc0)
            code = 0xe;
        else
            code = 0xb;
        command[9] = code;
        *(unsigned*)(command + 0xc) = tile;
        int tile_x = tile & 0xff, tile_z = (tile >> 16) & 0xff;
        int x = tile_x * 128 + 64, z = tile_z * 128 + 64;
        *(unsigned short*)(base + 0x178) = z;
        *(unsigned*)(base + 0x174) = ((unsigned)func_800E42D4(x, z) << 16) | (unsigned)x;
        unsigned speed;
        int kind;
        if (gait_speed(base, 0x4200, &speed, &kind) < 0) {
            command[9] = 2;
            return;
        }
        *(unsigned*)(command + 0x10) = speed;
        if (*(unsigned*)(command + 4) && !(tile & 0xff00))
            func_800E4C1C(tile_x, tile_z);
        return;
    }
    case 15: {
        unsigned* candidate = psx_load(unsigned*, base + 0x22c);
        unsigned slot = candidate[1], target = candidate[3];
        int row = slot & 7, column = (slot >> 3) & 3;
        *(unsigned*)(command + 0xc) = ((unsigned)column << 16) | (unsigned)row;
        unsigned slot_flags = *(unsigned*)(base + 0x230 + row * 32 + column * 8);
        if (!((1u << ((slot_flags >> 3) & 3)) & 6)) {
            unsigned char* model = D_800F4538[target & 0xf];
            *(unsigned*)(command + 0x10) =
                *(unsigned*)(model + 0x1c) - (((target >> 7) & 0xfff) << 16);
            *(unsigned short*)(command + 0x14) = *(unsigned short*)(model + 0x20);
        } else {
            *(unsigned*)(command + 0x10) = (((target >> 4) & 7) << 16) | (target & 0xf);
        }
        command[9] = 0x18;
        return;
    }
    case 16:
        command[9] = 6;
        *(unsigned*)(command + 0xc) = *(unsigned short*)(base + 0x188)
            | ((unsigned)*(unsigned short*)(base + 0x13a) << 16);
        return;
    case 17:
        command[9] = 8;
        return;
    default:
        pc_unimplemented("actor command beyond jtbl_800F16F8");
    }
}

/* 7BDD4.s: refresh the approach point (0xF4/0xF6, half-tile units) of the
 * target actor 0xE5: its waypoint group when it has one (model byte 0x5D for
 * plain models, tile byte 1 of 0x60 for flagged ones), else its position. */
void func_800E4904(void* state)
{
    unsigned char* base = state;
    int id = base[0xe5];
    unsigned char* model = D_800F4538[id];
    if (!((unsigned*)(0x1f800000 + 0x37c))[id])
        return;
    int group = -1, x, z;
    if (!(model[0xa] & 0x1f)) {
        if (model[0x5d] < 2) {
            x = *(short*)(model + 0x1c) >> 6;
            z = *(short*)(model + 0x20) >> 6;
        } else {
            group = model[0x5d] - 2;
        }
    } else {
        int byte = (*(unsigned*)(model + 0x60) & 0xff00) >> 8;
        if (byte) {
            group = byte - 2;
        } else {
            x = *(short*)(model + 0x4c) >> 6;
            z = *(short*)(model + 0x50) >> 6;
        }
    }
    if (group != -1) {
        unsigned* waypoint = func_8008E2D4(group);
        func_800DFA54(group, waypoint[0], waypoint[2], base + 0xf4);
        x = (signed char)base[0xf4] << 1;
        z = (signed char)base[0xf6] << 1;
    }
    base[0xf4] = x;
    base[0xf6] = z;
}
/* 7BDD4.s: func_800E4904 while in approach mode (0x89 == 1). */
void func_800E48F8(void* state)
{
    if (((unsigned char*)state)[0x89] == 1)
        func_800E4904(state);
}

extern void func_800E678C(void*);
/* 72B70.s: finish a pending stop request (0x12D): request 1 with a basic
 * command (< 9) idles (command 1); otherwise release the tile reservation
 * (func_800E678C), clear the request and switch to command 2. */
void func_800DB7B4(void* state)
{
    unsigned char* base = state;
    int request = base[0x12d];
    if (!request)
        return;
    if (request == 1 && (psx_load(unsigned char*, base + 0x54))[9] < 9) {
        func_800DEEFC(state, 1);
        return;
    }
    func_800E678C(state);
    base[0x12d] = 0;
    func_800DEEFC(state, 2);
}

/* 7BDD4.s: with no route (0x18C) and no cooldown (0x155), and a random draw
 * below `range` (always true for powers of two), set command byte 0xB to
 * `value` and start a 30-frame cooldown. The original computes a masked byte
 * but stores the raw value. */
void func_800E48A8(void* state, int value, int range)
{
    unsigned char* base = state;
    int roll = func_800E45D4(range);
    if (*(unsigned*)(base + 0x18c) | base[0x155])
        return;
    if (!(roll < range))
        return;
    base[0x155] = 30;
    (psx_load(unsigned char*, base + 0x54))[0xb] = value;
}

extern int func_8008D2C0(void*);
extern short func_8008DC7C(int, int);
extern unsigned char* D_800F45E0[];
/* 7E05C.s func_800E6BA0 (register ABI: the probe point in $s2-$s4): 1 unless
 * one of the room's obstacle points (func_8008D2C0) lies within 0x40 / 0x20 /
 * 0x40 of it (obstacle Y raised by 0x20). */
static int clear_of_obstacles(int x, int y, int z)
{
    unsigned points[8][2];
    int count = func_8008D2C0(points);
    if (count > 8)
        pc_unimplemented("obstacle point buffer overflow");
    for (int i = 0; i < count; ++i) {
        int dx = (int)(points[i][0] & 0xffff) - x;
        int dy = ((int)points[i][0] >> 16) + 0x20 - y;
        int dz = (short)points[i][1] - z;
        if (dx < 0)
            dx = -dx;
        if (dy < 0)
            dy = -dy;
        if (dz < 0)
            dz = -dz;
        if (dx < 0x40 && dy < 0x20 && dz < 0x40)
            return 0;
    }
    return 1;
}
/* 7E05C.s: trace an attack from `from` toward `to` in 32 steps of
 * (to - from) >> 5. Stop at the first actor (not `self`, not hidden by flags
 * 0xEA, not knocked down) whose bounds contain the point while in front of the
 * start, at an obstacle, or below the floor: store the point (pad = actor id
 * + 1, or 0) in `out` and return it. Returns 0 when nothing was hit. The start
 * X/Z are compared as GTE IR0/IR2 (16-bit), and the routine ends by falling
 * through into func_800E6EAC's return. The address comes back as a 32-bit
 * word: 146C.c declares the function int (an 8-byte pointer would be read
 * through a 4-byte return there). */
int func_800E6C34(unsigned* from, unsigned* to, unsigned* out, int self)
{
    int x = (short)from[0], y = (int)from[0] >> 16, z = (short)from[1];
    int start_x = (short)x, start_z = (short)z;
    int step_x = ((short)to[0] - x) >> 5, step_y = (((int)to[0] >> 16) - y) >> 5;
    int step_z = ((short)to[1] - z) >> 5;
    for (int steps = 0;;) {
        x += step_x;
        y += step_y;
        z += step_z;
        int hit = 0;
        for (int id = 0; id < 16 && !hit; ++id) {
            unsigned char* actor = vs_battle_actors[id];
            if (!actor || (actor[0x1c] & 0xea) || id == self)
                continue;
            unsigned char* body = D_800F45E0[id];
            int reach = 0x3f, height;
            if (body) {
                if (!body[0x12] || body[0x1a])
                    continue;
                height = 0x80;
            } else {
                body = D_800F4538[id];
                if (!body)
                    continue;
                int pose = body[0xb] & 0xf;
                reach = *(unsigned short*)(body + 0x63c);
                if (pose == 1 || pose == 2 || (body[0x5af] & 1) == 1)
                    continue;
                height = *(unsigned short*)(body + 0x63e);
            }
            unsigned position = *(unsigned*)(body + 0x1c);
            height >>= 1;
            int px = (short)position, py = ((int)position >> 16) - height;
            int pz = *(unsigned short*)(body + 0x20);
            int ahead = (px - start_x) * step_x + step_z * (pz - start_z);
            int dx = px - x, dy = py - y, dz = pz - z;
            if (dx < 0)
                dx = -dx;
            if (dy < 0)
                dy = -dy;
            if (dz < 0)
                dz = -dz;
            if (ahead < 0)
                continue;
            if ((dx < reach) & (dy < height) & (dz < reach))
                hit = id + 1;
        }
        int pad = hit;
        if (!hit && clear_of_obstacles(x, y, z)
            && y < (int)((unsigned)func_8008DC7C(x, z) << 17) >> 17) {
            if (++steps < 32)
                continue;
            return 0;
        }
        out[0] = (unsigned)(x & 0xffff) | ((unsigned)y << 16);
        out[1] = (unsigned)(z & 0xffff) | ((unsigned)pad << 16);
        return psx_to_int(int, out);
    }
}

/* 7DE94.s: release actor `id`'s tile reservation (func_800E678C body) and
 * mark it held (0x1A = 1). The original enters func_800E678C's body with the
 * flag value; setting it afterwards is equivalent (nothing reads it there). */
void func_800E6764(int id)
{
    unsigned char* state = D_800F5878[id];
    if (!state)
        return;
    func_800E678C(state);
    state[0x1a] = 1;
}
/* 7DE94.s: release the reservation of actor `id` when its command (byte 9)
 * is one of the set 0xA41800 and its model has no motion flags (0xA & 0x1F,
 * 0xB & 0xB). */
void func_800E6700(int id)
{
    unsigned char* actor = vs_battle_actors[id];
    unsigned char* model = D_800F4538[id];
    if (!actor || !((1u << (actor[9] & 31)) & 0xa41800))
        return;
    if ((model[0xa] & 0x1f) | (model[0xb] & 0xb))
        return;
    if (D_800F5878[id])
        func_800E678C(D_800F5878[id]);
}

/* 72B70.s: resolve a step onto a cell flagged 0x20 (e.g. a ledge or gap):
 * keep stepping in `dir` while cells stay flagged; at the first clear cell
 * within min(0xB2, 3) steps, return it with the step/entry result
 * (func_800D96C8, then func_800D954C) in bits 24-31. Unflagged targets are
 * returned unchanged. As in the original, the flagged-cell walk continues
 * past the step limit while byte 0xB2 is nonzero. */
unsigned func_800DB93C(void* state, unsigned tile, int dir, unsigned from)
{
    unsigned char* base = state;
    unsigned char* cells = SCRATCH_PTR(unsigned char*, 0x3c0);
    tile &= 0xffffff;
    int x = tile & 0xff, z = tile >> 16, limit = base[0xb2];
    if (!(*(unsigned short*)(cells + z * 64 + x * 2) & 0x20))
        return tile;
    int packed = SCRATCH8(0x3ec + dir), result = 0;
    int max_steps = limit < 3 ? limit : 3;
    for (int steps = 1;; ++steps) {
        x = (x + direction_dx(packed)) & 0xff;
        z = (z + direction_dz(packed)) & 0xff;
        tile = ((unsigned)z << 16) | (unsigned)x;
        if (*(unsigned short*)(cells + z * 64 + x * 2) & 0x20) {
            if (limit)
                continue;
            break;
        }
        if (steps < max_steps) {
            result = func_800D96C8(state, from, tile, dir);
            if (result == 1)
                result = func_800D954C(state, tile);
        }
        break;
    }
    return tile | ((unsigned)result << 24);
}

/* 7BDD4.s: remember the best candidate's target (when there is one and the
 * actor is not locked, byte 0x19): 0x38 = target << 24 | current tile,
 * 0x3C = the target's tile, 0x40 = 900-frame memory. */
void func_800E4C8C(void* state)
{
    unsigned char* base = state;
    if (*(int*)(base + 0x470) <= 0 || base[0x19])
        return;
    unsigned target = (psx_load(unsigned*, base + 0x430))[3] & 0xf;
    *(unsigned*)(base + 0x38) = (target << 24) | (*(unsigned*)(base + 0x34) & 0xffffff);
    *(unsigned*)(base + 0x3c) = *(unsigned*)(D_800F4538[target] + 0x5c);
    *(unsigned short*)(base + 0x40) = 900;
}

/* Room-entry and actor bookkeeping helpers from 7BDD4.s and 7E05C.s. */
extern unsigned D_800F58D0, D_800F5920;
extern void* vs_main_allocHeap(unsigned);
extern void func_800E4288(void*, unsigned);

/* 7BDD4.s: pick the first armed reaction from the packed bytes at 0x224:
 * flag 0x20 (code 10) then 0x10 (code 9) when byte 1 is set, the reverse
 * otherwise. Low byte flags select byte 2, byte 1 flags select byte 3. */
int func_800E4764(void* state, int* value, int* code)
{
    unsigned char* base = state;
    unsigned packed = *(unsigned*)(base + 0x224);
    unsigned low = packed & 0xff, high = (packed >> 8) & 0xff;
    int bits[2] = { 0x20, 0x10 }, codes[2] = { 10, 9 };
    int order = base[1] ? 0 : 1;
    for (int pass = 0; pass < 2; ++pass, order ^= 1) {
        int bit = bits[order];
        if ((low & bit) || (high & bit)) {
            *value = low & bit ? (packed >> 16) & 0xff : packed >> 24;
            *code = codes[order];
            return 1;
        }
    }
    /* With byte 1 set the original keeps alternating between both tests
     * forever; with it clear it returns 0 after the 0x10 then 0x20 tests. */
    if (base[1])
        pc_unimplemented("func_800E4764 endless reaction scan");
    return 0;
}

/* 7BDD4.s: clear the reaction word, copy 0x34 to 0x170 and issue actor
 * command 11 (func_800E4B10) or 12 (func_800E4B18). */
void func_800E4B10(void* state)
{
    unsigned char* base = state;
    *(unsigned*)(base + 0x228) = 0;
    *(unsigned*)(base + 0x170) = *(unsigned*)(base + 0x34);
    func_800DEEFC(state, 11);
}
void func_800E4B18(void* state)
{
    unsigned char* base = state;
    *(unsigned*)(base + 0x228) = 0;
    *(unsigned*)(base + 0x170) = *(unsigned*)(base + 0x34);
    func_800DEEFC(state, 12);
}

/* 7BDD4.s: clear (func_800E4BD8) or set (func_800E4BE0) bit `bit` of the
 * room record's byte 0xE. Bit 31 inverts the sign test, as in the original. */
static void room_flag(int bit, int clear)
{
    unsigned char* room = psx_to_ptr(unsigned char*, D_800F58BC);
    if (!room)
        return;
    int mask = 1 << (bit & 31);
    if (clear)
        mask = -1 - mask;
    room[0xE] = mask < 0 ? room[0xE] & mask : room[0xE] | mask;
}
void func_800E4BD8(int bit) { room_flag(bit, 1); }
void func_800E4BE0(int bit) { room_flag(bit, 0); }

/* 7BDD4.s: set actor `id`'s 0x13F countdown to 30 and fall through into
 * func_800E4C8C with that actor. */
void func_800E4C64(int id)
{
    unsigned char* state;
    if (!id || !(state = D_800F5878[id]))
        return;
    state[0x13f] = 30;
    func_800E4C8C(state);
}

/* 7E05C.s: adjust the occupancy count of the tile under an actor's placement
 * word (0x5C) by +1 (func_800E6898) or -1 (func_800E68A0), in steps of 0x400. */
static void tile_occupancy(unsigned char* actor, int delta)
{
    unsigned placement = *(unsigned*)(actor + 0x5c);
    if (!D_800F58B8)
        return;
    unsigned short* cell =
        psx_to_ptr(unsigned short*, D_800F58D0 + (placement & 0xff) * 2 + ((placement << 8) >> 18));
    *cell += delta << 10;
}
void func_800E6898(void* actor) { tile_occupancy(actor, 1); }
void func_800E68A0(void* actor) { tile_occupancy(actor, -1); }

/* 7E05C.s: mark navigation cell (x, z) blocked (0x400). */
void func_800E68EC(int x, int z)
{
    if (!D_800F58B8)
        return;
    *psx_to_ptr(unsigned short*, D_800F58B8 + x * 2 + z * 64) |= 0x400;
}

/* 7E05C.s: lowest of 16 floor heights sampled from (x0, z0) toward (x1, z1)
 * in steps of a sixteenth, starting at 0 (heights are 15-bit signed). */
int func_800E6A6C(int x0, int z0, int x1, int z1)
{
    int dx = (x1 - x0) >> 4, dz = (z1 - z0) >> 4, lowest = 0;
    for (int i = 0; i < 16; ++i) {
        x0 += dx;
        z0 += dz;
        int height = (int)((unsigned)func_800E4318(x0, z0) << 17) >> 17;
        if (height <= lowest)
            lowest = height;
    }
    return lowest;
}

/* 7E05C.s: clear actor `id`'s hold flag (0x1A). */
void func_800E6B24(int id)
{
    unsigned char* state = D_800F5878[id & 0xff];
    if (state)
        state[0x1a] = 0;
}

/* 7E05C.s: when actor `id` is of type 11 or 14, its room-flag word (0x170,
 * bits 8-15) is set and its model byte 0xC has a zero low nibble, clear that
 * room flag. A missing record reads PS1 low RAM, as the original does. */
void func_800E6EB0(int id)
{
    unsigned char* actor = vs_battle_actors[id];
    unsigned char* state = D_800F5878[id];
    if (!actor && !state)
        return;
    if (!state)
        state = (unsigned char*)0x80000000u;
    unsigned type = actor ? actor[9] : *(unsigned char*)0x80000009u;
    if (!((1u << (type & 31)) & 0x4800))
        return;
    unsigned flags = *(unsigned*)(state + 0x170) & 0xff00;
    unsigned char* model = psx_load(unsigned char*, state + 0x58);
    if (!flags || (model[0xc] & 0xf))
        return;
    func_800E4BD8(flags >> 8);
}

/* 7E05C.s: actors whose 0x16 target is `id` drop it (0x10), then release
 * actor `id`'s reservation through func_800D82CC. */
void func_800E7608(int id)
{
    unsigned char* node = vs_battle_actors[0];
    do {
        unsigned char* state = D_800F5878[*(unsigned*)(node + 4)];
        if (state[0x16] == (unsigned)id)
            state[0x16] = 0x10;
        node = psx_load(unsigned char*, node);
    } while (node);
    func_800D82CC(D_800F5878[id]);
}

/* 7E05C.s: allocate the zeroed 32-byte record D_800F5920 with 0xC = 900. */
void func_800E7660(void* state)
{
    (void)state;
    unsigned char* record = vs_main_allocHeap(0x20);
    D_800F5920 = psx_to_int(unsigned, record);
    func_800E4288(record, 0x20);
    *(unsigned short*)(record + 0xc) = 0x384;
}

/* Engagement state helpers from 7399C.s and 73B0C.s. */
extern int rand(void);
extern void func_800D8260(void*, int, int);

/* 7399C.s: leave the engaged state (0x13): restore the saved movement
 * callback (0x6C -> 0x68) and, unless 0x30 is set, queue the recovery motion. */
void func_800DC19C(void* state)
{
    unsigned char* base = state;
    base[0x126] = 0;
    if (!base[0x13])
        return;
    base[0x13] = 0;
    func_800D820C(base[0x88], 0x40);
    func_800E7370(state);
    unsigned saved = *(unsigned*)(base + 0x6c);
    *(unsigned*)(base + 0x6c) = 0;
    if (saved)
        *(unsigned*)(base + 0x68) = saved;
    if (base[0x30])
        return;
    func_800D8260(state, 6, 0x28);
    func_800E48A8(state, 2, 0x80);
}

/* 7399C.s: enter the engaged state when forced, or on the second of two
 * rand() & 0x7F < 0x4C rolls; save 0x68 to 0x6C and install D_800DBC0C. */
void func_800DC210(void* state, int force)
{
    unsigned char* base = state;
    if (!force) {
        if ((rand() & 0x7f) >= 0x4c)
            return;
        if (++base[0x126] < 2)
            return;
    }
    base[0x13] = 1;
    unsigned saved = *(unsigned*)(base + 0x68);
    psx_store(base + 0x68, behaviour_800DBC0C);
    *(unsigned*)(base + 0x6c) = saved;
    func_800D820C(base[0x88], 0x80);
}

/* 73B0C.s: move *value by `step` (up when mode is 2, else down) and clamp it
 * to 0..*limit. */
void func_800DC30C(short* value, short* limit, int step, int mode)
{
    int next = mode == 2 ? *value + step : *value - step;
    *value = next < 0 ? 0 : *limit < next ? *limit : next;
}
