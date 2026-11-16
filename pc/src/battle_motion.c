/* Native equivalents of the actor motion-request routines that BATTLE.PRG
 * 4A0A8.c keeps as original inline assembly (excluded under ROOD_PC).
 * Model layout offsets follow the original (vs_actors_actorRecords entries). Branch
 * delay slots are executed, as in the byte-matched original. */
#include "common.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/146C.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/3A1A0.h"

extern void vs_actors_beginActorJumpToTile(void*, void*, int, int);
extern int vs_actors_playActorAnimation(int, int, int, int);
extern void vs_actors_turnActorToAtRate(int, int, int);
extern void vs_actors_playActorTurnAnimation(int, int);
extern void vs_actors_turnActorToAtAngularSpeed(int, int, int);

/* Clear the motion flags (0xA bits 5-6) and set bit 6, then hand the target
 * to vs_actors_beginActorJumpToTile. */
/* Play animation 0x1F (blend 8) before moving: sets motion bits 5-6. */
static void motion_animated(int id, D_800F4538_t* model, int* target, int speed, int facing)
{
    ((unsigned char*)&model->unk0)[0xa] |= 0x60; /* the header's flag bytes */
    vs_actors_playActorAnimation(id, 0x1f, 0, 8);
    vs_actors_beginActorJumpToTile(model, target, speed, facing);
}
/* 4A0A8.c vs_actors_beginTileJump: start a move; actors already moving restart it
 * (bit 6) or animate first (bit 5); otherwise mark state 0x5B0 = 1. */
void vs_actors_beginTileJump(int id, int* target, int speed, int facing);

void vs_actors_beginAirborneTileJump(int id, int* target, int speed, int facing);

void vs_actors_beginNavigationJump(int id, int* target, int speed, int facing)
{
    motion_animated(id, vs_actors_actorRecords[id], target, speed, facing);
}
/* The motion request block (unk1848): the kind word, the packed pair, a word and the position. */
void func_800B64CC(int id, int a, int b, int c)
{
    D_800F4538_unk1848* request = &vs_actors_actorRecords[id]->unk1848;
    *(unsigned*)&request->unk4 = ((unsigned)c << 16) | (unsigned)a; /* unk4 and unk6 as one word */
    request->unk8 = b;
    *(unsigned*)&request->unk0 = 1; /* the kind */
}
void vs_actors_setDirectMovement(int id, int a, int b);

void vs_actors_queueGroundOffset(int id, unsigned* position);

void vs_actors_queueFlightMovement(int id, unsigned* position, int b, int c);

void vs_actors_queueMovementVector(int id, unsigned* position, int b);

/* Start a turn toward position[0] at rate b unless one is pending (0xB low
 * nibble); also applies to the linked actor 0x17FD when its id is >= 2. */
void vs_actors_beginTeleport(int id, int* position, int b);

void func_800B65E8(int id, int* position, int b);

void func_800B65F4(int id, int* position, int b);

/* Walk (0x20) or run (0x80, when 0x63C >= 0x80), then vs_actors_playActorTurnAnimation. */
void vs_actors_turnNavigationActor(int id, int a)
{
    D_800F4538_t* model = vs_actors_actorRecords[id];
    vs_actors_turnActorToAtRate(id, a, model->unk63C < 0x80 ? 0x20 : 0x80);
    vs_actors_playActorTurnAnimation(id, a);
}
void vs_actors_turnActorAtSpeed(int id, short a, int b);

