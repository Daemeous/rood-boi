/* Native translations of actor-unload assembly in 7E05C.s, 7DE94.s,
 * 7BDD4.s and 6FA0C.s. Guest structure offsets retain the original ABI. */
#include "psx_types.h" /* psx_load/psx_store: 4-byte guest pointers */
extern unsigned char *D_800F5878[], *vs_battle_actors[];
/* Guest words at 0x800F58xx: 32-bit guest addresses. */
extern unsigned D_800F58B8, D_800F58BC, D_800F590C, D_800F5920;
extern void func_800E5240(unsigned);
extern void func_80095B7C(int, int);
extern void func_800DBF00(void*);
extern void vs_main_freeHeap(void*);

void func_800E4C28(int x, int z)
{
    if (D_800F58B8) {
        unsigned short* cell = psx_to_ptr(unsigned short*, D_800F58B8 + (unsigned)x * 2 + (unsigned)z * 64);
        *cell &= 0xff7f;
    }
}
void func_800D821C(void* actor)
{
    unsigned id = ((unsigned char*)actor)[0x88];
    if (id) {
        func_80095B7C(id, 0);
        func_80095B7C(id, 0x40);
    }
}
void func_800E678C(void* actor)
{
    unsigned char* state = actor;
    unsigned char* grid = psx_to_ptr(unsigned char*, D_800F58BC);
    unsigned x = state[0x170], z = state[0x172];
    unsigned active = state[0x19b];
    state[0x1a] = 0;
    if (x >= grid[1] && x <= grid[0] && z >= grid[3] && z <= grid[2])
        func_800E4C28(x, z);
    if (active) {
        unsigned value = grid[0xe];
        /* The original uses the byte itself as a masked variable shift. */
        grid[0xe] = value & (0u - (1u << (value & 31)));
    }
}
void func_800E7454(void* actor)
{
    unsigned char* node = actor;
    if (!D_800F58B8)
        return;
    unsigned id = *(unsigned*)(node + 4);
    if (node[0x1c] & 0x14)
        D_800F590C -= 1u << (id & 31);
    for (unsigned char* peer = vs_battle_actors[0]; peer; peer = psx_load(void*, peer)) {
        unsigned char* state = D_800F5878[*(unsigned*)(peer + 4)];
        if (state[0x16] == id)
            state[0x16] = 0x10;
    }
    unsigned char* grid = psx_to_ptr(unsigned char*, D_800F58BC);
    for (int offset = 0x34; offset >= 0x28; offset -= 4)
        if (psx_load(void*, grid + offset) == actor)
            psx_store(grid + offset, (void*)0);
    if (!(node[0x1c] & 5))
        return;
    unsigned char* state = D_800F5878[id];
    func_800E5240(id);
    func_800E678C(state);
    if (!id)
        return;
    unsigned short* bits = (void*)(grid + 0x10);
    unsigned bit = (1u << (id & 31)) & *bits;
    if (bit) {
        *bits -= bit;
        --grid[0x12];
    }
    node[0xb] &= 0xfc;
    if (state) {
        func_800D821C(state);
        func_800DBF00(state);
        vs_main_freeHeap(state);
        D_800F5878[id] = 0;
    }
    unsigned type = *(unsigned short*)(node + 0x1a);
    if (type == 0xac || type == 0xad) {
        void* data = psx_to_ptr(void*, D_800F5920);
        D_800F5920 = 0;
        vs_main_freeHeap(data);
    }
}

extern unsigned D_800F58D0, D_800F58C0;
/* 7E05C.s: unload the remaining linked actors, then release navigation storage. */
void func_800E6F1C(void)
{
    for (unsigned char* node = vs_battle_actors[0]; node; node = psx_load(void*, node))
        func_800E7454(node);
    D_800F58D0 = 0;
    D_800F58B8 = 0;
    D_800F58C0 = 0;
    vs_main_freeHeap(psx_to_ptr(void*, D_800F58BC));
    D_800F58BC = 0;
}

/* 7E05C.s: the room's height limit (word 0x14 of the D_800F58BC record), or 0
 * when no record is loaded. Used when placing actors on room entry. */
int func_800E75EC(void)
{
    unsigned char* room = psx_to_ptr(unsigned char*, D_800F58BC);
    return room ? *(int*)(room + 0x14) : 0;
}
