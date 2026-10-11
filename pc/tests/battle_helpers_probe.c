#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
void func_800E4288(void*, unsigned);
void func_800E6828(void*);
void func_800E685C(int, int, int);
int func_800E4DF8(void*, int, int);
void func_800DC810(void*);
void func_800DC3E8(void*, void*);
void func_800E4F14(void*);
void func_800DC424(void*);
void func_800DC2E0(void*, void*);
int func_800DC484(int, int);
int func_800DC48C(int, int);
void func_800DC4F0(void*, void*);
int func_800E4CF4(void*, void*, int);
void func_800DC344(void*, void*);
void func_800DC3CC(void*);
void func_800D9DD8(void*);
void func_800E511C(void);
int func_800D820C(int, int);
int func_800D826C(void*);
void func_800D8280(void*);
void func_800E4358(void*);
int func_800E4624(short*, short*, int, int);
int func_800E4478(int, int, int);
int func_800E44E8(short*, short*, int, int);
static int sqr_args[3];
int func_800E4660(int x, int y, int z)
{
    sqr_args[0] = x, sqr_args[1] = y, sqr_args[2] = z;
    return 7;
}
static int sent[8], sent_count;
void func_80095B7C(int id, int value)
{
    sent[sent_count++] = id;
    sent[sent_count++] = value;
}
unsigned D_800F58B8, D_800F590C;
int D_800F58D4;
static char driver_log[8];
static int driver_count;
void func_800E65DC(void) { driver_log[driver_count++] = 'S'; }
void func_800D8EF4(void) { driver_log[driver_count++] = 'a'; }
void func_800DAED0(void) { driver_log[driver_count++] = 'b'; }
void func_800DC0D0(void) { driver_log[driver_count++] = 'c'; }
void func_800D9538(void) { driver_log[driver_count++] = 'd'; }
int func_800DAA6C(void* state) { return state != NULL; }
static void* waypoint_destination;
void func_800DD7CC(void* state, void* destination) { waypoint_destination = destination; }
unsigned char* D_800F5904;
int D_800F58F0;
unsigned D_800F58F4;
unsigned char* D_800F58F8;
void vs_main_memcpy(void* dst, void* src, unsigned size) { memcpy(dst, src, size); }
unsigned char vs_main_actions[0x34 * 4];
short func_800E6694(int);
void pc_unimplemented(const char* name)
{
    fprintf(stderr, "%s\n", name);
    exit(2);
}
unsigned char* vs_battle_actors[2];
unsigned char* D_800F5878[2];
unsigned char D_80069C0C[] = { 3, 2, 1, 1, 1, 2, 2, 3 };
static int pass_log[8], pass_count;
void func_800DD000(void* state, void* context)
{
    if (context != (void*)pass_log)
        exit(1);
    pass_log[pass_count++] = state == D_800F5878[0] ? 10 : 11;
}
void func_800DD344(void* state) { pass_log[pass_count++] = state == D_800F5878[0] ? 20 : 21; }
void func_800E7370(void*);
void func_800E72D0(void);
static unsigned rolls[4], roll_count, reactions;
int rand(void) { return (int)rolls[roll_count++ & 3]; }
void func_800D8260(void* state, int a, int b) { reactions += a == 6 && b == 0x28 && state; }
void func_800E48A8(void* state, int a, int b) { reactions += a == 2 && b == 0x80 && state; }
void func_800DC574(void*, int);
void func_800DC784(void*, int);
unsigned char vs_battle_roomData[0x80];
int func_80099514(unsigned, unsigned, unsigned);
static int callback_count;
void func_800DC210(void* state, int enabled)
{
    if (state != D_800F5878[1] || enabled != 1)
        exit(1);
    ++callback_count;
}
unsigned* pc_sprite_packet(unsigned*, unsigned*, int, unsigned, unsigned, unsigned);
int main(void)
{
    unsigned words[10];
    for (unsigned size = 4; size <= 32; size += 4) {
        memset(words, 0xa5, sizeof(words));
        func_800E4288(words, size);
        for (unsigned i = 0; i < 10; ++i)
            if (words[i] != (i < size / 4 ? 0u : 0xa5a5a5a5u))
                return 1;
    }
    unsigned char actor[0x230];
    /* Check both input bytes, priority when both groups are set, and untouched data. */
    for (unsigned flags = 0; flags < 65536; ++flags) {
        memset(actor, 0xa5, sizeof(actor));
        actor[0x224] = flags;
        actor[0x225] = flags >> 8;
        func_800E6828(actor);
        unsigned merged = flags | (flags >> 8);
        unsigned expected = (merged & 0x30) ? 1 : ((merged & 8) ? 2 : 0);
        if (actor[0x125] != expected || actor[0x15e] != (merged & 255)
            || actor[0x15f] != (merged >> 8) || actor[0x124] != 0xa5
            || actor[0x126] != 0xa5)
            return 1;
    }
    /* Stat pair: signed maximum and packed halfword; empty slots are ignored. */
    D_800F5878[0] = NULL;
    func_800E685C(0, 1, 2);
    memset(actor, 0xa5, sizeof(actor));
    D_800F5878[0] = actor;
    func_800E685C(0, 7, 40);
    if (actor[0xb3] != 40 || actor[0x226] != 7 || actor[0x227] != 40 || actor[0x228] != 0xa5)
        return 1;
    func_800E685C(0, 90, -1);
    if (actor[0xb3] != 90 || actor[0x226] != 90 || actor[0x227] != 0xff)
        return 1;
    /* Actor evaluation: both passes walk the whole list in order. */
    {
        static unsigned char other[0x10];
        unsigned second_node[2] = { 0, 1 }, first_node[2] = { (unsigned)second_node, 0 };
        unsigned char* saved_actor = vs_battle_actors[0];
        D_800F5878[0] = actor;
        D_800F5878[1] = other;
        vs_battle_actors[0] = (void*)first_node;
        func_800DC810(pass_log);
        if (pass_count != 4 || pass_log[0] != 10 || pass_log[1] != 11 || pass_log[2] != 20
            || pass_log[3] != 21)
            return 1;
        vs_battle_actors[0] = saved_actor;
    }
    /* Action usability: row count, per-type resource checks, flag bit 7. */
    static unsigned char usable_state[0x250], usable_owner[0x40], usable_stats[0x950];
    *(unsigned char**)(usable_state + 0x54) = usable_owner;
    *(unsigned char**)(usable_owner + 0x3c) = usable_stats;
    *(short*)(usable_stats + 0xdc + 0x398) = 2;
    for (int type = 0; type < 4; ++type) {
        vs_main_actions[type * 0x34 + 2] = (unsigned char)((type + 1) << 1);
        vs_main_actions[type * 0x34 + 3] = 10;
        *(unsigned short*)(usable_stats + 0x8c0 + (4 + type) * 4) = type;
    }
    *(short*)(usable_stats + 0x1c) = 10;
    *(unsigned*)(usable_stats + 0x18) = (8u << 16) | 13;
    *(short*)(usable_stats + 0x14c) = 9;
    *(unsigned*)(usable_state + 0x230 + 4 * 8) = 0x101;
    if (func_800E4DF8(usable_state, 1, 0) != 1 || *(unsigned*)(usable_state + 0x230 + 4 * 8) != 0x181)
        return 1;
    if (func_800E4DF8(usable_state, 1, 1) != 1 || func_800E4DF8(usable_state, 1, 2) != 0
        || func_800E4DF8(usable_state, 1, 3) != 0)
        return 1;
    *(unsigned*)(usable_stats + 0x18) = (8u << 16) | 14;
    *(unsigned*)(usable_stats + 0x948) = 0x1000;
    if (func_800E4DF8(usable_state, 1, 2) != 1 || func_800E4DF8(usable_state, 1, 0) != 0)
        return 1;
    *(unsigned*)(usable_stats + 0x948) = 0x2000;
    if (func_800E4DF8(usable_state, 1, 1) != 1 || func_800E4DF8(usable_state, 1, 2) != 0
        || *(unsigned*)(usable_state + 0x230 + 6 * 8) != 0)
        return 1;
    *(short*)(usable_stats + 0xdc + 0x398) = 1;
    if (func_800E4DF8(usable_state, 1, 1) != 0)
        return 1;
    /* Actor stat copy: four words and the 0x528-byte block, nothing else. */
    {
        static unsigned char from[0x964], to[0x964];
        for (int i = 0; i < 0x964; ++i)
            from[i] = (unsigned char)(i * 7 + 1);
        memset(to, 0xa5, sizeof(to));
        func_800DC3E8(from, to);
        if (memcmp(to + 0x18, from + 0x18, 8) || memcmp(to + 0x30, from + 0x30, 4)
            || memcmp(to + 0x948, from + 0x948, 4) || memcmp(to + 0x398, from + 0x398, 0x528)
            || to[0x17] != 0xa5 || to[0x20] != 0xa5 || to[0x34] != 0xa5 || to[0x397] != 0xa5
            || to[0x8c0] != 0xa5 || to[0x94c] != 0xa5)
            return 1;
    }
    /* Condition bytes, including the overwritten 0x90000 mask (0xE2B0 only). */
    *(unsigned*)(usable_stats + 0x18) = (100u << 16) | 49;
    *(unsigned*)(usable_stats + 0x1c) = (60u << 16) | 31;
    *(unsigned*)(usable_stats + 0x948) = 0x90000;
    usable_state[0x17] = 0;
    func_800E4F14(usable_state);
    if (usable_state[0x12] != 1 || usable_state[0x11] != 0 || usable_state[0x10] != 0)
        return 1;
    usable_state[0x17] = 32;
    *(unsigned*)(usable_stats + 0x948) = 0x1000;
    func_800E4F14(usable_state);
    if (usable_state[0x11] != 1 || usable_state[0x10] != 1)
        return 1;
    usable_state[0x17] = 31;
    *(unsigned*)(usable_stats + 0x18) = (100u << 16) | 50;
    *(unsigned*)(usable_stats + 0x948) = 0x20;
    func_800E4F14(usable_state);
    if (usable_state[0x12] != 0 || usable_state[0x11] != 0 || usable_state[0x10] != 1)
        return 1;
    /* Stat snapshot records indexed by actor id. */
    {
        unsigned node1[16] = { 0, 2 }, node0[16] = { (unsigned)node1, 0 }, snap[18];
        node0[15] = (unsigned)usable_stats;
        node1[15] = (unsigned)usable_stats;
        *(unsigned*)(usable_stats + 0x150) = 0x150;
        *(unsigned*)(usable_stats + 0x2b4) = 0x2b4;
        memset(snap, 0xa5, sizeof(snap));
        unsigned char* saved_actor = vs_battle_actors[0];
        vs_battle_actors[0] = (void*)node0;
        func_800DC424(snap);
        vs_battle_actors[0] = saved_actor;
        if (snap[0] != 0 || snap[1] != *(unsigned*)(usable_stats + 0x18)
            || snap[3] != *(unsigned*)(usable_stats + 0x948) || snap[4] != 0x150
            || snap[5] != 0x2b4 || snap[6] != 0xa5a5a5a5u || snap[12] != 2
            || snap[14] != *(unsigned*)(usable_stats + 0x1c))
            return 1;
    }
    /* Candidate copy, remembered-candidate list and target bits. */
    {
        unsigned src[6] = { 1, (2u << 5) | (1u << 3) | 1, 3, 4, 5, 6 }, dst[6] = { 0 };
        static unsigned char remembered[0x14 * 17];
        unsigned short info[8] = { 0 };
        func_800DC2E0(dst, src);
        if (memcmp(dst, src, 20) || dst[5] != 0)
            return 1;
        D_800F58F8 = remembered;
        D_800F58F0 = 15;
        D_800F58F4 = 0;
        func_800DC4F0(src, info);
        if (D_800F58F0 != 15 || D_800F58F4 != 0)
            return 1;
        info[7] = 0x40;
        func_800DC4F0(src, info);
        if (D_800F58F0 != 16 || D_800F58F4 != (1u << 5) || memcmp(remembered + 15 * 0x14, src, 20))
            return 1;
        src[1] += 1; /* row 2: a different slot bit, but the list is full */
        func_800DC4F0(src, info);
        if (D_800F58F0 != 16 || D_800F58F4 != ((1u << 5) | (1u << 9)))
            return 1;
        unsigned char* saved_actor = vs_battle_actors[0];
        unsigned char actor_node[0x40] = { 0 };
        *(unsigned char**)(actor_node + 0x3c) = usable_stats;
        vs_battle_actors[0] = actor_node;
        *(unsigned short*)(vs_main_actions + 2 * 0x34 + 0xe) = 0x8000 | 0x4000 | 0x2000;
        usable_stats[0x37] = 2;
        if (func_800DC484(2, 0) != 1 || func_800DC48C(2, 0) != 1)
            return 1;
        *(unsigned short*)(vs_main_actions + 0x60 * 0x34 + 0xe) = 0xc000;
        if (func_800DC484(0x60, 0) != 0 || func_800DC48C(0x60, 0) != 2)
            return 1;
        vs_battle_actors[0] = saved_actor;
        /* Targeting: relation, mask, self and option bits. */
        static unsigned char target_state[0x260], peer[0x20];
        unsigned cand[5] = { 0, (2u << 5) | 1, 0, 0, 0 };
        D_800F5878[1] = peer;
        if (func_800E4CF4(target_state, cand, 0) != 0)
            return 1;
        *(unsigned*)(target_state + 0x1a4 + 4) = 1u << 6;
        if (func_800E4CF4(target_state, cand, 1) != 0)
            return 1;
        *(unsigned*)(target_state + 0x1a4 + 4) = 2u << 6;
        if (func_800E4CF4(target_state, cand, 1) != 1)
            return 1;
        cand[4] = 0x20;
        *(unsigned short*)(vs_main_actions + 2 * 0x34 + 0xe) = 2u << 9;
        if (func_800E4CF4(target_state, cand, 1) != 0)
            return 1;
        *(unsigned*)(peer + 0x10) = 0x100;
        if (func_800E4CF4(target_state, cand, 1) != 1)
            return 1;
        cand[4] = 0x10;
        if (func_800E4CF4(target_state, cand, 1) != 0)
            return 1;
        cand[4] = 0;
        target_state[0x88] = 1;
        *(unsigned*)(target_state + 0x230 + 4 * 8) = 2;
        if (func_800E4CF4(target_state, cand, 1) != 0)
            return 1;
    }
    /* Scored candidate list: weight, capacity, used-action marks. */
    {
        static unsigned char list_state[0x480], used[16 * 256];
        unsigned cand[5] = { 7, 9u << 5, 0, (3u << 19) | 2, 11 };
        D_800F5904 = used;
        func_800DC344(list_state, cand);
        if (*(int*)(list_state + 0x470) != 1 || used[2 * 256 + 9] != 1
            || *(unsigned char**)(list_state + 0x430) != list_state + 0x2f0
            || memcmp(list_state + 0x2f0, cand, 20))
            return 1;
        cand[3] = 2;
        func_800DC344(list_state, cand);
        *(int*)(list_state + 0x470) = 16;
        cand[3] = (1u << 19) | 3;
        func_800DC344(list_state, cand);
        if (*(int*)(list_state + 0x470) != 16 || used[3 * 256 + 9] != 0)
            return 1;
    }
    /* Candidate sort: descending scores over the pointer table. */
    {
        static unsigned char sort_state[0x480];
        int scores[7] = { 5, -3, 9, 5, 0, 12, 1 }, expected[7] = { 12, 9, 5, 5, 1, 0, -3 };
        for (int i = 0; i < 7; ++i)
            ((int**)(sort_state + 0x430))[i] = &scores[i];
        *(int*)(sort_state + 0x470) = 7;
        func_800DC3CC(sort_state);
        for (int i = 0; i < 7; ++i)
            if (*((int**)(sort_state + 0x430))[i] != expected[i])
                return 1;
        *(int*)(sort_state + 0x470) = 0;
        func_800DC3CC(sort_state);
    }
    /* Movement mode: running callback, candidate mask, then waypoint search. */
    {
        static unsigned char move_state[0x480];
        unsigned best[5] = { 0, 0, 0, 0, 0x04 };
        *(unsigned**)(move_state + 0x430) = best;
        *(void**)(move_state + 0x68) = (void*)func_800DAA6C;
        move_state[0x2d] = 0x04;
        func_800D9DD8(move_state);
        if (move_state[0x12e] != 1 || waypoint_destination)
            return 1;
        *(void**)(move_state + 0x68) = NULL;
        move_state[0x2d] = 0x02;
        func_800D9DD8(move_state);
        if (move_state[0x12e] != 1 || waypoint_destination)
            return 1;
        move_state[0x2d] = 0x06;
        func_800D9DD8(move_state);
        if (move_state[0x12e] != 2 || waypoint_destination != move_state + 0x134)
            return 1;
    }
    /* AI driver: inactive skip, scratch cache, next active actor, call order. */
    func_800E511C();
    if (driver_count)
        return 1;
    D_800F58B8 = 0x1234;
    D_800F590C = (1u << 3) | (1u << 9);
    D_800F58D4 = 3;
    func_800E511C();
    if (driver_count != 5 || memcmp(driver_log, "Sabcd", 5) || D_800F58D4 != 9)
        return 1;
    driver_count = 0;
    func_800E511C();
    if (D_800F58D4 != 3)
        return 1;
    /* Actor timer and message helpers. */
    {
        static unsigned char timer_state[0x140];
        if (func_800D820C(0, 5) != 1 || sent_count)
            return 1;
        timer_state[0x88] = 3;
        *(unsigned short*)(timer_state + 0x138) = 0x77;
        func_800D8280(timer_state);
        if (sent_count)
            return 1;
        timer_state[0x12a] = 2;
        func_800D8280(timer_state);
        func_800D8280(timer_state);
        if (timer_state[0x12a] != 0 || sent_count != 4 || sent[1] != 0x77 || sent[3] != 0)
            return 1;
        timer_state[0x155] = 1;
        timer_state[0x13f] = 0;
        func_800E4358(timer_state);
        func_800E4358(timer_state);
        if (timer_state[0x155] != 0 || timer_state[0x13f] != 0)
            return 1;
        timer_state[0x13f] = 5;
        func_800E4358(timer_state);
        if (timer_state[0x13f] != 4 || timer_state[0x154] != 0 || timer_state[0x140] != 0)
            return 1;
        short from[3] = { 10, -4, 30 }, to[3] = { 3, 6, -2 };
        if (func_800E4624(from, to, 5, -1) != 7 || sqr_args[0] != 7 || sqr_args[1] != -4
            || sqr_args[2] != 32)
            return 1;
        *(unsigned short*)(timer_state + 0x8e) = 0x1f;
        if (func_800D826C(timer_state) != 0)
            return 1;
        *(unsigned short*)(timer_state + 0x8e) = 0x20;
        if (func_800D826C(timer_state) != 1)
            return 1;
    }
    /* Floor test and 16-sample line of sight over a scratchpad-cached grid. */
    {
        unsigned* scratch = mmap((void*)0x1f800000, 0x1000, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        static unsigned short grid[32 * 32];
        if (scratch != (unsigned*)0x1f800000)
            return 1;
        scratch[0x3bc / 4] = (unsigned)grid;
        grid[1 * 32 + 2] = 0x2000 | 0x3ff; /* blocked flag: floor 0 */
        grid[0] = 0x010 | 0x400; /* (16 << 6) - (0x400 >> 3) = 1024 - 128 */
        grid[3] = 0x3f0; /* sign-extended: (0x3f0 << 6) as short = -1024 */
        if (func_800E4478(300, -1, 140) != 1 || func_800E4478(300, 0, 140) != 0)
            return 1;
        if (func_800E4478(0, 895, 0) != 1 || func_800E4478(0, 896, 0) != 0)
            return 1;
        if (func_800E4478(3 * 128, -1025, 0) != 1 || func_800E4478(3 * 128, -1024, 0) != 0)
            return 1;
        short from[3] = { 0, 2000, 0 }, to[3] = { 100, 2000, 0 };
        if (func_800E44E8(from, to, 0, 0) != 1)
            return 1;
        from[1] = to[1] = 0;
        if (func_800E44E8(from, to, 0, 0) != 0)
            return 1;
        if (func_800E44E8(from, to, -2000, -2000) != 1)
            return 1;
    }
    D_800F5878[0] = D_800F5878[1] = NULL;
    unsigned states[2][0x484 / 4] = { { 0 } };
    if (func_800E6694(0) != -1)
        return 1;
    D_800F5878[0] = (void*)states[0];
    /* Round-trip all packed disposition fields (bit 0 is a separate callback). */
    unsigned probe_node[2] = { 0, 0 };
    vs_battle_actors[0] = (void*)probe_node;
    for (unsigned options = 0; options < 0x2000; options += 2) {
        *(short*)(D_800F5878[0] + 0x42) = options;
        D_800F5878[0][0x13] = 1;
        D_800F5878[0][0x12d] = !(options & 32);
        func_800E72D0();
        if (func_800E6694(0) != (short)(options | 1))
            return 1;
    }
    memset(states, 0, sizeof(states));
    unsigned nodes[2][2] = { { (unsigned)&nodes[1], 0 }, { 0, 1 } };
    vs_battle_actors[0] = (void*)nodes[0];
    D_800F5878[0] = (void*)states[0];
    D_800F5878[1] = (void*)states[1];
    D_800F5878[1][0x88] = 1;
    D_800F5878[1][0x2c] = 2;
    states[0][0x1a4 / 4 + 1] = 0x12340001;
    func_800E7370(states[0]);
    if (states[0][0x1a4 / 4] != 0x80 || states[0][0x1a4 / 4 + 1] != 0x12340081
        || states[1][0x1a4 / 4] != 0x40 || states[1][0x1a4 / 4 + 1] != 0x80)
        return 1;
    func_800E7370(states[0]); /* stable on repeat; no recursive loop */
    *(short*)(D_800F5878[0] + 0x42) = -1;
    *(short*)(D_800F5878[1] + 0x42) = 0x1fff;
    func_800E72D0();
    if (states[0][0x1a4 / 4] != 0x80 || states[1][0x1a4 / 4] != 0x37f000c0
        || callback_count != 1)
        return 1;
    short collision[24] = { 2, 1, 0, 2, 0x0800, 1, 1, 0 };
    *(short**)(vs_battle_roomData + 0x68) = collision;
    signed char* tiles = (signed char*)(collision + 8);
    tiles[0] = 3;
    tiles[15] = -2;
    tiles[16] = 7;
    if (func_80099514(0, 0, 0) != 7 || func_80099514(3, 3, 0) != 130
        || func_80099514(4, 0, 0) != 7 || func_80099514(0, 0, 1) != 7)
        return 1;
    unsigned packet[6], next = 0xab123456;
    if (pc_sprite_packet(packet, &next, 0x180, 0x00200010, 0x00080008, 0xe100000c)
            != packet
        || packet[0] != 0x05123456 || packet[1] != 0xe100000c || packet[2] != 0x66808080
        || packet[3] != 0x00200010 || packet[4] != 0 || packet[5] != 0x00080008
        || next != ((unsigned)packet & 0xffffff))
        return 1;
    {
        /* Candidate lead shuffle: entry 1 has no weight, entry 2 wins its
         * roll (0x10 < min(0x60, 0x4C)); the rest re-sort by score. */
        static unsigned char plan[0x480];
        unsigned c[4][4] = { { 5, 0x20, 0, 0x60u << 19 }, { 9, 0, 0, 0x60u << 19 },
            { 3, 0x20, 0, 0x60u << 19 }, { 7, 0x20, 0, 0x60u << 19 } };
        unsigned** list = (unsigned**)(plan + 0x430);
        for (int i = 0; i < 4; ++i)
            list[i] = c[i];
        rolls[0] = 0x7f, rolls[1] = 0x7f, rolls[2] = 0x90, roll_count = 0;
        func_800DC574(plan, 4);
        if (roll_count != 3 || list[0] != c[2] || list[1] != c[1] || list[2] != c[3]
            || list[3] != c[0])
            return 1;
        /* Target classification 3, reacting once. */
        *(unsigned*)(plan + 0x1a4 + 8) = 0x12347f41;
        func_800DC784(plan, 2);
        if (*(unsigned*)(plan + 0x1a4 + 8) != 0x123400c1 || plan[0x18] != 1 || reactions != 2)
            return 1;
        func_800DC784(plan, 2);
        if (reactions != 2)
            return 1;
    }
    puts("PASS: battle clear spans, actor flags and reciprocal relations, candidate shuffle, target classification");
    return 0;
}
