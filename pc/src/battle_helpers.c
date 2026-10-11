/* Native equivalents of hand-written game assembly, added only when verified. */
/* src/BATTLE/BATTLE.PRG/7E05C.s: func_800E6EAC returns immediately.
 * Its delay slot only writes caller-saved $t0 (the following function's lui). */
#include "psx_types.h" /* psx_load/psx_store: 4-byte guest pointers */
void func_800E6EAC(void) { }

extern void pc_unimplemented(const char*);
/* 7B5DC.s: clear complete words; original callers pass positive word sizes. */
void func_800E4288(void* destination, unsigned bytes)
{
    unsigned* words = destination;
    if (bytes < 4 || ((__UINTPTR_TYPE__)destination & 3))
        pc_unimplemented("battle zero-fill invalid span");
    for (unsigned i = 0; i < (bytes >> 2); ++i)
        words[i] = 0;
}

/* 7E028.s: merge the two actor flags and derive their priority state.
 * Explicit offsets preserve the assembly ABI until this structure is shared. */
void func_800E6828(void* actor)
{
    unsigned char* state = actor;
    unsigned flags = state[0x224] | ((unsigned)state[0x225] << 8);
    flags |= flags >> 8;
    state[0x15e] = flags;
    state[0x15f] = flags >> 8;
    state[0x125] = (flags & 0x30) ? 1 : ((flags & 8) ? 2 : 0);
}

extern unsigned char* vs_battle_actors[];
extern unsigned char* D_800F5878[];
extern unsigned char D_80069C0C[];
/* 7E05C.s: store an actor's two stat bytes and their maximum; empty actor
 * slots are ignored. The original's return value is not meaningful. */
void func_800E685C(int id, int first, int second)
{
    unsigned char* actor = D_800F5878[id];
    if (!actor)
        return;
    actor[0xb3] = first < second ? second : first;
    unsigned packed = ((unsigned)second << 8) | (unsigned)first;
    actor[0x226] = packed;
    actor[0x227] = packed >> 8;
}
/* 7E05C.s: propagate reciprocal actor relations through the linked actor list.
 * The reciprocal field is read before writing the outgoing field, including
 * the self-relation case; preserving that order preserves recursion behavior. */
void func_800E7370(void* actor)
{
    unsigned char* state = actor;
    for (unsigned char* node = vs_battle_actors[0]; node; node = psx_load(unsigned char*, node)) {
        int id = *(int*)(node + 4);
        unsigned char* peer = D_800F5878[id];
        unsigned self = state[0x88];
        unsigned relation = 2;
        if (self != (unsigned)id) {
            unsigned column = !((id < 1) | (peer[0x2c] >= 3));
            relation = D_80069C0C[state[0x2c] * 2 + column];
        }
        unsigned* outgoing = (unsigned*)(state + 0x1a4 + id * 4);
        unsigned reciprocal = *(unsigned*)(peer + 0x1a4 + self * 4);
        *outgoing = (*outgoing & ~0xc0u) | (relation << 6);
        if (!(reciprocal & 0xc0))
            func_800E7370(peer);
    }
}

extern void func_800DC210(void*, int);
/* 7E05C.s: apply packed actor options. Negative options leave relations intact. */
void func_800E72D0(void)
{
    for (unsigned char* node = vs_battle_actors[0]; node; node = psx_load(unsigned char*, node)) {
        int id = *(int*)(node + 4);
        unsigned char* state = D_800F5878[id];
        int options = *(short*)(state + 0x42);
        if (options < 0)
            continue;
        unsigned* flags = (unsigned*)(state + 0x1a4);
        *flags = (*flags & 0xc80fff30u) | (((options >> 3) & 3u) << 28)
               | (((options >> 1) & 3u) << 6) | (((options >> 6) & 127u) << 20);
        if (options & 1)
            func_800DC210(state, 1);
    }
}

extern unsigned char vs_battle_roomData[];
/* 2EA3C.s: unpack a 4x4 collision tile from the selected height layer. */
int func_80099514(unsigned x, unsigned z, unsigned layer)
{
    short* map = psx_load(short*, vs_battle_roomData + 0x68);
    unsigned cells = map[3];
    unsigned cell = (unsigned)map[0] * (z >> 2) + (x >> 2);
    unsigned packed = (unsigned short)map[4 + layer * cells + cell];
    signed char* tiles = (signed char*)(map + 4 + cells * 2);
    int value = tiles[(packed & 0x7ff) * 16 + (z & 3) * 4 + (x & 3)];
    return ((value + ((packed & 0xf800) >> 9)) & 0x7f) | (value & 0x80);
}

/* 573B8.c inline-assembly sprite entry points. */
unsigned* pc_sprite_packet(unsigned* packet, unsigned* next, int brightness, unsigned xy,
    unsigned wh, unsigned page)
{
    unsigned color = brightness & 255;
    packet[0] = (*next & 0xffffff) | 0x05000000;
    packet[1] = page;
    packet[2] =
        color | (color << 8) | (color << 16) | ((100u + ((brightness >> 7) & 2)) << 24);
    packet[3] = xy;
    packet[4] = 0;
    packet[5] = wh;
    *next = psx_to_int(unsigned, packet) & 0xffffff;
    return packet;
}
unsigned* vs_battle_setSprite(int brightness, int xy, int wh, unsigned* next)
{
    unsigned* packet = pc_sprite_packet(psx_load(unsigned*, 0x1f800000), next, brightness, xy, wh, 0);
    psx_store(0x1f800000, packet + 6);
    return packet;
}
unsigned* vs_battle_setSpriteDefaultTexPage(
    int brightness, int xy, int wh, unsigned* next)
{
    unsigned* packet = vs_battle_setSprite(brightness, xy, wh, next);
    packet[1] = 0xe100000c;
    return packet;
}
unsigned* vs_battle_setSpriteDefault(int wh, int xy)
{
    return vs_battle_setSpriteDefaultTexPage(0x80, xy, wh, psx_load(unsigned*, 0x1f800008));
}

/* 7DE94.s: preserve actor disposition when unloading a room. */
short func_800E6694(int id)
{
    unsigned char* state = D_800F5878[id];
    if (!state)
        return -1;
    unsigned flags = *(unsigned*)(state + 0x1a4);
    return state[0x13] | (((flags >> 6) & 3) << 1) | (((flags >> 28) & 3) << 3)
         | ((state[0x12d] == 0) << 5) | (((flags >> 20) & 127) << 6);
}

extern unsigned char vs_main_actions[];
/* 7C4F4.s: decide whether action slot (row, column) is usable and record it in
 * bit 7 of the slot's flags. Type 1 checks a signed 16-bit resource and a
 * blocking flag; type 3 compares cost plus half the high stat against the low
 * stat; type 4 checks a second signed resource. A 0x2000 state allows only
 * types 1 and 2. Rows with a count below 2 are never usable. */
int func_800E4DF8(void* state, int row, int column)
{
    unsigned char* base = state;
    unsigned char* stats = psx_load(unsigned char*, psx_load(unsigned char*, base + 0x54) + 0x3c);
    int slot = row * 4 + column, usable = 0;
    unsigned action = *(unsigned short*)(stats + 0x8c0 + slot * 4);
    unsigned flags = *(unsigned*)(stats + 0x948);
    unsigned pair = *(unsigned*)(stats + 0x18);
    if (*(short*)(stats + row * 0xdc + 0x398) >= 2) {
        unsigned char* entry = vs_main_actions + action * 0x34;
        int type = (entry[2] >> 1) & 7, cost = entry[3];
        if (!(flags & 0x2000) || ((1 << type) & 6)) {
            if (type == 1)
                usable = !(*(short*)(stats + 0x1c) < cost) && !(flags & 0x1000);
            else if (type == 3)
                usable = !((int)(pair & 0xffff) < cost + (int)((pair >> 16) >> 1));
            else if (type == 4)
                usable = !(*(short*)(stats + 0x14c) < cost);
            else
                usable = 1;
        }
    }
    unsigned* slot_flags = (unsigned*)(base + 0x230 + slot * 8);
    *slot_flags = (*slot_flags & ~0x80u) | ((unsigned)usable << 7);
    return usable;
}

extern void func_800DD000(void*, void*);
extern void func_800DD344(void*);
/* 73C84.s: run func_800DD000 for every actor, then func_800DD344 for every
 * actor (the second pass also receives the context in $a1, which it ignores).
 * The original walks the list as a do-while; an empty list would fault. */
void func_800DC810(void* context)
{
    unsigned char* node;
    if (!vs_battle_actors[0])
        pc_unimplemented("actor evaluation with empty actor list");
    for (node = vs_battle_actors[0]; node; node = psx_load(unsigned char*, node))
        func_800DD000(D_800F5878[*(int*)(node + 4)], context);
    for (node = vs_battle_actors[0]; node; node = psx_load(unsigned char*, node))
        func_800DD344(D_800F5878[*(int*)(node + 4)]);
}

extern void vs_main_memcpy(void*, void*, unsigned);
/* 73B0C.s: copy an actor's stat words (0x18, 0x1c, 0x30, 0x948) and the
 * 0x528-byte block at 0x398 from src to dst, tail-calling vs_main_memcpy. */
void func_800DC3E8(void* src, void* dst)
{
    unsigned char *from = src, *to = dst;
    *(unsigned*)(to + 0x18) = *(unsigned*)(from + 0x18);
    *(unsigned*)(to + 0x1c) = *(unsigned*)(from + 0x1c);
    *(unsigned*)(to + 0x948) = *(unsigned*)(from + 0x948);
    *(unsigned*)(to + 0x30) = *(unsigned*)(from + 0x30);
    vs_main_memcpy(to + 0x398, from + 0x398, 0x528);
}

/* 7C4F4.s: derive three condition bytes from the actor's paired 16-bit stats
 * (low = current, high = maximum) and the state threshold at 0x17:
 * 0x12 = low1 * 2 < high1; 0x11 = threshold and (low2 < threshold or
 * low2 * 2 < high2); 0x10 = status flags. The original loads 0x90000 with lui
 * and then overwrites it with ori $zero, so the mask is 0xE2B0. */
void func_800E4F14(void* state)
{
    unsigned char* base = state;
    unsigned char* stats = psx_load(unsigned char*, psx_load(unsigned char*, base + 0x54) + 0x3c);
    unsigned first = *(unsigned*)(stats + 0x18), second = *(unsigned*)(stats + 0x1c);
    unsigned flags = *(unsigned*)(stats + 0x948);
    int threshold = base[0x17];
    int low = second & 0xffff, high = second >> 16;
    base[0x12] = (int)((first & 0xffff) * 2) < (int)(first >> 16);
    base[0x11] = threshold && (low < threshold || low * 2 < high);
    base[0x10] = (flags & 0xe2b0) || (threshold && (flags & 0x1000));
}

/* 73C24.s: snapshot each listed actor's id and stat words into a 0x18-byte
 * record indexed by id. Like the original do-while, an empty list faults. */
void func_800DC424(void* snapshots)
{
    unsigned char* node = vs_battle_actors[0];
    if (!node)
        pc_unimplemented("actor stat snapshot with empty actor list");
    for (; node; node = psx_load(unsigned char*, node)) {
        int id = *(int*)(node + 4);
        unsigned char* stats = psx_load(unsigned char*, node + 0x3c);
        unsigned* record = (unsigned*)((unsigned char*)snapshots + id * 0x18);
        record[0] = id;
        record[1] = *(unsigned*)(stats + 0x18);
        record[2] = *(unsigned*)(stats + 0x1c);
        record[3] = *(unsigned*)(stats + 0x948);
        record[4] = *(unsigned*)(stats + 0x150);
        record[5] = *(unsigned*)(stats + 0x2b4);
    }
}

/* 73AE0.s: copy one 0x14-byte action candidate (dst, src). */
void func_800DC2E0(void* dst, void* src)
{
    unsigned *to = dst, *from = src;
    for (int i = 0; i < 5; ++i)
        to[i] = from[i];
}

/* 73C84.s: bit 15 (func_800DC484) or 14 (func_800DC48C) of the action's
 * target flags. For action 0x60 against an actor of class 2 the original
 * returns the whole shifted halfword xor 1, not just the inverted bit. */
static int action_target_bit(int action, int id, int bit)
{
    unsigned char* stats = psx_load(unsigned char*, vs_battle_actors[id] + 0x3c);
    unsigned flags = *(unsigned short*)(vs_main_actions + action * 0x34 + 0xe) >> bit;
    if (action == 0x60 && (stats[0x37] & 7) == 2)
        return flags ^ 1;
    return flags & 1;
}
int func_800DC484(int action, int id) { return action_target_bit(action, id, 15); }
int func_800DC48C(int action, int id) { return action_target_bit(action, id, 14); }

extern int D_800F58F0;
extern unsigned D_800F58F4;
extern unsigned D_800F58F8; /* a 32-bit guest address */
/* 73C84.s: remember the first candidate for each action slot whose action has
 * flag 0x40, up to 16 entries. The slot bit is set even when the list is full. */
void func_800DC4F0(void* candidate, void* info)
{
    unsigned word = ((unsigned*)candidate)[1];
    unsigned bit = 1u << ((word & 7) * 4 + ((word >> 3) & 3));
    if (!(*(unsigned short*)((unsigned char*)info + 0xe) & 0x40) || (D_800F58F4 & bit))
        return;
    D_800F58F4 |= bit;
    if (D_800F58F0 < 16)
        func_800DC2E0(psx_to_ptr(unsigned char*, D_800F58F8) + D_800F58F0++ * 0x14, candidate);
}

/* 7C4F4.s: whether a candidate may target actor id. Requires a peer and a
 * relation >= 2; candidate bit 5 adds a relation < 3 check against the action's
 * target mask (unless action flag 0x100); bit 4 rejects relation < 3; a slot
 * flag bit 1 rejects targeting oneself. */
int func_800E4CF4(void* state, void* candidate, int id)
{
    unsigned char* base = state;
    unsigned* entry = candidate;
    unsigned char* peer = D_800F5878[id];
    unsigned word = entry[1], options = entry[4];
    int slot = (word & 7) * 4 + ((word >> 3) & 3);
    if (!peer)
        return 0;
    unsigned relation = (*(unsigned*)(base + 0x1a4 + id * 4) >> 6) & 3;
    if (relation < 2)
        return 0;
    if (options & 0x20) {
        unsigned peer_flags = *(unsigned*)(peer + 0x10);
        unsigned mask = ((peer_flags >> 7) & 2) | ((peer_flags >> 14) & 4) | (peer_flags & 1);
        unsigned action_flags =
            *(unsigned short*)(vs_main_actions + ((word >> 5) & 0xff) * 0x34 + 0xe);
        if (relation >= 3)
            return 0;
        if (!(action_flags & 0x100) && !((action_flags >> 9) & mask))
            return 0;
    }
    if ((options & 0x10) && relation < 3)
        return 0;
    if (((*(unsigned*)(base + 0x230 + slot * 8) >> 1) & 1) && id == base[0x88])
        return 0;
    return 1;
}

extern unsigned D_800F5904; /* a 32-bit guest address */
/* 73B0C.s: append a scored candidate (up to 16, nonzero weight) to the state's
 * list at 0x2F0, record its pointer at 0x430, and mark (target, action) used. */
void func_800DC344(void* state, void* candidate)
{
    unsigned char* base = state;
    unsigned* entry = candidate;
    int count = *(int*)(base + 0x470);
    if (count >= 16 || !((entry[3] >> 19) & 0xff))
        return;
    psx_to_ptr(unsigned char*, D_800F5904)[((entry[3] & 0xf) << 8) + ((entry[1] >> 5) & 0xff)] = 1;
    unsigned char* record = base + 0x2f0 + count * 0x14;
    psx_store(base + 0x430 + count * 4, record);
    *(int*)(base + 0x470) = count + 1;
    func_800DC2E0(record, candidate);
}

/* 7E05C.s func_800E6974: K&R quicksort of candidate pointers by their first
 * word (score), descending. The original packs left/right into bytes when
 * saving them, which is lossless for the 16-entry lists it sorts. The list
 * holds 4-byte guest pointers (PS1 layout of the AI record). */
static int candidate_score(unsigned address) { return *psx_to_ptr(int*, address); }
static void sort_candidates(unsigned* v, int left, int right)
{
    unsigned swap;
    if (left >= right)
        return;
    int mid = (left + right) >> 1, last = left;
    swap = v[left], v[left] = v[mid], v[mid] = swap;
    for (int i = left + 1; i <= right; ++i)
        if (candidate_score(v[left]) < candidate_score(v[i])) {
            ++last;
            swap = v[last], v[last] = v[i], v[i] = swap;
        }
    swap = v[last], v[last] = v[left], v[left] = swap;
    sort_candidates(v, left, last - 1);
    sort_candidates(v, last + 1, right & 0xff);
}
/* 73B0C.s: sort the state's candidate pointers (0x430, count at 0x470). */
void func_800DC3CC(void* state)
{
    unsigned char* base = state;
    int count = *(int*)(base + 0x470);
    if (count > 256)
        pc_unimplemented("candidate sort beyond byte-packed bounds");
    if (count)
        sort_candidates((unsigned*)(base + 0x430), 0, count - 1);
}

extern int rand(void);
/* 73C84.s: give the first `count` candidates (0x430) one chance each, in
 * order, to take the lead: a candidate with a nonzero byte at bit 5 of word 4
 * wins when rand() & 0x7F is below its weight (bits 19-26 of word 0xC,
 * capped at 0x4C); it swaps with the first entry and the rest are re-sorted
 * (L800E6958, the func_800E6974 quicksort from index 1). */
void func_800DC574(void* state, int count)
{
    unsigned* list = (unsigned*)((unsigned char*)state + 0x430); /* guest pointers */
    if (count > 16)
        pc_unimplemented("candidate shuffle beyond the 16-entry list");
    for (int i = 0; i < count; ++i) {
        unsigned roll = (unsigned)rand() & 0x7f;
        unsigned* candidate = psx_to_ptr(unsigned*, list[i]);
        if (!((candidate[1] >> 5) & 0xff))
            continue;
        unsigned weight = (candidate[3] >> 19) & 0xff;
        if (roll < (weight < 0x4c ? weight : 0x4c)) {
            list[i] = list[0];
            list[0] = psx_to_int(unsigned, candidate);
            sort_candidates(list, 1, count - 1);
            return;
        }
    }
}
extern void func_800D8260(void*, int, int);
extern void func_800E48A8(void*, int, int);
/* 73C84.s: mark target `id` (word 0x1A4 + id * 4) as classification 3 and
 * clear bits 8-14. Unless it already was, the actor first reacts: byte 0x18
 * is set (the original's rand() & 0x7F < 0x80 always holds, but rand is
 * still drawn), then func_800D8260(6, 0x28) and func_800E48A8(2, 0x80). */
void func_800DC784(void* state, int id)
{
    unsigned char* base = state;
    unsigned* target = (unsigned*)(base + 0x1a4 + id * 4);
    unsigned flags = *target;
    if (((flags >> 6) & 3) < 3) {
        rand();
        base[0x18] = 1;
        func_800D8260(state, 6, 0x28);
        func_800E48A8(state, 2, 0x80);
    }
    *target = (flags & 0xffff803fu) | 0xc0;
}

extern int func_800DAA6C(void*);
extern void func_800DD7CC(void*, void*);
/* 70D4C.s: choose the movement mode at 0x12E. Actors already running
 * func_800DAA6C, or whose best candidate's flags miss the mask at 0x2D, use
 * mode 1; otherwise mode 2 and a waypoint search into 0x134. */
void func_800D9DD8(void* state)
{
    unsigned char* base = state;
    unsigned* best = psx_load(unsigned*, base + 0x430);
    if (psx_load(void*, base + 0x68) == (void*)func_800DAA6C || !(best[4] & base[0x2d])) {
        base[0x12e] = 1;
        return;
    }
    base[0x12e] = 2;
    func_800DD7CC(base, base + 0x134);
}

extern unsigned D_800F58B8;
extern unsigned D_800F590C;
extern int D_800F58D4;
extern void func_800E65DC(void); /* pc/src/actor_update.c */
extern void func_800D8EF4(void);
extern void func_800DAED0(void);
extern void func_800DC0D0(void);
extern void func_800D9538(void);
/* 7C7E0.s: per-frame AI driver. The original walks the null-terminated
 * routine table D_800F16BC: func_800E65DC, 0x800E50C8 (advance to the next
 * active actor), 0x800E5100 (move $sp to the scratchpad at 0x1F800374),
 * func_800D8EF4, func_800DAED0, func_800DC0D0, 0x800E5110 (restore $sp) and
 * func_800D9538. The table is fixed data, so it is called directly here; the
 * stack switch is a speed trick and is not reproduced (the host stack does not
 * overwrite scratchpad below 0x374 as the original's frames would). */
void func_800E511C(void)
{
    if (!D_800F58B8)
        return;
    func_800E65DC();
    /* 0x800E50C8: next set bit of D_800F590C after D_800F58D4, modulo 16.
     * An empty mask would spin forever in the original. */
    if (!(D_800F590C & 0xffff))
        pc_unimplemented("AI driver with no active actors");
    int id = D_800F58D4;
    do
        id = (id + 1) & 15;
    while (!(D_800F590C & (1u << id)));
    D_800F58D4 = id;
    func_800D8EF4();
    func_800DAED0();
    func_800DC0D0();
    func_800D9538();
}

extern void func_80095B7C(int, int);
/* 6FA0C.s: forward (id, value) to func_80095B7C for nonzero actor ids. For id 0
 * the original falls into func_800D826C's tail and returns 1; otherwise $v0 is
 * whatever func_80095B7C leaves (callers ignore it). */
int func_800D820C(int id, int value)
{
    if (!id)
        return 1;
    func_80095B7C(id, value);
    return 0;
}
/* func_800D821C (6FA0C.s) is in pc/src/actor_cleanup.c. */
/* 6FA6C.s: whether the halfword at 0x8E is at least 0x20. */
int func_800D826C(void* state) { return !(*(unsigned short*)((unsigned char*)state + 0x8e) < 0x20); }
/* 6FA6C.s: count down the timer at 0x12A. On reaching zero send 0 (L800D8254),
 * otherwise send the halfword at 0x138, both for the state's actor id. */
void func_800D8280(void* state)
{
    unsigned char* base = state;
    if (!base[0x12a])
        return;
    if (--base[0x12a] == 0)
        func_800D820C(base[0x88], 0);
    else
        func_800D820C(base[0x88], *(unsigned short*)(base + 0x138));
}

/* 7B5DC.s: count down the two per-actor byte timers at 0x155 and 0x13F,
 * stopping at zero. */
void func_800E4358(void* state)
{
    unsigned char* base = state;
    if (base[0x155])
        --base[0x155];
    if (base[0x13f])
        --base[0x13f];
}

extern int func_800E4660(int, int, int);
/* 7BDD4.s: squared distance between two short XYZ positions, with height
 * offsets added to each Y; falls through into func_800E4660 (GTE SQR). */
int func_800E4624(short* a, short* b, int a_height, int b_height)
{
    return func_800E4660(a[0] - b[0], (a[1] + a_height) - (b[1] + b_height), a[2] - b[2]);
}

/* 7B5DC.s: whether height y is above the floor of the navigation cell at
 * (x >> 7, z >> 7) in the grid (64-byte rows) cached at scratchpad 0x3BC. A cell with
 * bit 13 set has floor 0; otherwise floor = sign15(sign((cell & 0x3FF) << 6)
 * - ((cell & 0x1C00) >> 3)). Returns 1 when y < floor. */
int func_800E4478(int x, int y, int z)
{
    unsigned char* grid = psx_load(unsigned char*, 0x1f800000 + 0x3bc);
    unsigned cell = *(unsigned short*)(grid + (z >> 7) * 64 + (x >> 7) * 2);
    int floor = 0;
    if (!(cell & 0x2000)) {
        int height = (int)(cell << 22) >> 16;
        floor = (int)((unsigned)(height - (int)((cell & 0x1c00) >> 3)) << 17) >> 17;
    }
    return y < floor;
}
/* 7B5DC.s: sample 16 points from `from` toward `to` (each Y lowered by its
 * height argument, X read as unsigned 16-bit) in steps of delta >> 4; return 1
 * as soon as a sample has y >= floor (func_800E4478 returns 0; Y grows down). */
int func_800E44E8(short* from, short* to, int from_height, int to_height)
{
    int x = (unsigned short)from[0], y = from[1] - from_height, z = from[2];
    int dx = ((int)(unsigned short)to[0] - x) >> 4;
    int dy = ((to[1] - to_height) - y) >> 4;
    int dz = (to[2] - z) >> 4;
    for (int i = 0; i < 16; ++i) {
        x += dx;
        y += dy;
        z += dz;
        if (!func_800E4478(x, y, z))
            return 1;
    }
    return 0;
}
