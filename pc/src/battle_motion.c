/* Native equivalents of the actor motion-request routines that BATTLE.PRG
 * 4A0A8.c keeps as original inline assembly (excluded under ROOD_PC).
 * Model layout offsets follow the original (D_800F4538 entries). Branch
 * delay slots are executed, as in the byte-matched original. */
extern unsigned char* D_800F4538[];
extern void func_800A208C(void*, void*, int, int);
extern int func_800A0204(int, int, int, int);
extern void func_800A9EB4(int, int, int);
extern void func_800A249C(int, int);
extern void func_800AA8D0(int, int, int);

/* Clear the motion flags (0xA bits 5-6) and set bit 6, then hand the target
 * to func_800A208C. */
static void motion_reset(unsigned char* model, int* target, int speed, int facing)
{
    model[0xa] = (model[0xa] & 0x9f) | 0x40;
    func_800A208C(model, target, speed, facing);
}
/* Play animation 0x1F (blend 8) before moving: sets motion bits 5-6. */
static void motion_animated(int id, unsigned char* model, int* target, int speed, int facing)
{
    model[0xa] |= 0x60;
    func_800A0204(id, 0x1f, 0, 8);
    func_800A208C(model, target, speed, facing);
}
/* 4A0A8.c func_800B63AC: start a move; actors already moving restart it
 * (bit 6) or animate first (bit 5); otherwise mark state 0x5B0 = 1. */
void func_800B63AC(int id, int* target, int speed, int facing)
{
    unsigned char* model = D_800F4538[id];
    if (model[0xa] & 0x40) {
        motion_reset(model, target, speed, facing);
        return;
    }
    if (model[0xa] & 0x20) {
        motion_animated(id, model, target, speed, facing);
        return;
    }
    model[0x5b0] = (model[0x5b0] & 0xfc) | 1;
    func_800A208C(model, target, speed, facing);
}
void func_800B6404(int id, int* target, int speed, int facing)
{
    motion_reset(D_800F4538[id], target, speed, facing);
}
void func_800B6438(int id, int* target, int speed, int facing)
{
    motion_animated(id, D_800F4538[id], target, speed, facing);
}
/* Motion request blocks at 0x1848 (kind), 0x184C, 0x1850, 0x1858/0x185C. */
void func_800B64CC(int id, int a, int b, int c)
{
    unsigned char* model = D_800F4538[id];
    *(unsigned*)(model + 0x184c) = ((unsigned)c << 16) | (unsigned)a;
    *(int*)(model + 0x1850) = b;
    *(unsigned*)(model + 0x1848) = 1;
}
/* func_800B64CC with c = a, then clear byte 0x1848 (kind becomes 0). */
void func_800B64A8(int id, int a, int b)
{
    func_800B64CC(id, a, b, a);
    D_800F4538[id][0x1848] = 0;
}
void func_800B6500(int id, unsigned* position)
{
    unsigned char* model = D_800F4538[id];
    *(unsigned*)(model + 0x1858) = position[0];
    *(unsigned*)(model + 0x185c) = position[1];
    *(unsigned*)(model + 0x1848) = 2;
}
void func_800B653C(int id, unsigned* position, int b, int c)
{
    unsigned char* model = D_800F4538[id];
    *(int*)(model + 0x1850) = b;
    *(unsigned short*)(model + 0x184e) = c;
    *(unsigned*)(model + 0x1858) = position[0];
    *(unsigned*)(model + 0x185c) = position[1];
    model[0xa] |= 0x20;
    *(unsigned*)(model + 0x1848) = 3;
}
void func_800B6530(int id, unsigned* position, int b)
{
    func_800B653C(id, position, b, b);
    *(unsigned*)(D_800F4538[id] + 0x1848) = 4;
}
/* Start a turn toward position[0] at rate b unless one is pending (0xB low
 * nibble); also applies to the linked actor 0x17FD when its id is >= 2. */
void func_800B6580(int id, int* position, int b)
{
    unsigned char* model = D_800F4538[id];
    if (model[0xb] & 0xf)
        return;
    int linked = model[0x17fd];
    *(unsigned short*)(model + 0x1a) = 0;
    *(int*)(model + 0x5ec) = position[0];
    *(unsigned*)(model + 0x5f0) = (unsigned)b << 16;
    model[0xb] = 1;
    if (linked >= 2)
        func_800B6580(linked, position, b);
}
static void look_request(int id, int* position, int b, int kind)
{
    unsigned char* model = D_800F4538[id];
    *(int*)(model + 0x5ec) = position[0];
    *(int*)(model + 0x5f0) = position[1];
    *(unsigned short*)(model + 0x5f2) = b;
    model[0xb] = (model[0xb] & 0x8f) | kind;
}
void func_800B65E8(int id, int* position, int b) { look_request(id, position, b, 0x10); }
void func_800B65F4(int id, int* position, int b) { look_request(id, position, b, 0x40); }
/* Walk (0x20) or run (0x80, when 0x63C >= 0x80), then func_800A249C. */
void func_800B6634(int id, int a)
{
    unsigned char* model = D_800F4538[id];
    func_800A9EB4(id, a, *(unsigned short*)(model + 0x63c) < 0x80 ? 0x20 : 0x80);
    func_800A249C(id, a);
}
void func_800B668C(int id, short a, int b)
{
    func_800AA8D0(id, a, b);
    func_800A249C(id, a);
}
