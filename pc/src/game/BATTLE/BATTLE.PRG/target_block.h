#pragma once
#include "58578.h"

/* The battle menu's block (vs_battle_targetSphere, one heap block): the target shapes and the cursor, the grid of the map
 * screen, the rows of the item and target lists. Every file that reaches into it shares this one definition. */

typedef struct {
    short xyz[3], flags;
} menuCircleVertex;

/* A target-list source row (the battle's list of candidates). */
typedef struct {
    int unk0;
    int unk4;
    u_char unk8;
    u_char unk9;
    u_char unkA;
    u_char unkB;
    u_short unkC;
    u_short unkE;
    u_int unk10;
    u_char unk14;
    u_char unk15;
    u_char unk16;
    u_char unk17;
} func_800C5798_t;

typedef union {
    SVECTOR vec;
    int words[2];
} func_800C4794_t;

/* One drawn row of the list (D_800EB9B8_t.unk990). */
typedef struct {
    char unk0;
    char unk1;
    char unk2;
    char unk3;
    short unk4;
    char unk6;
    char unk7;
    func_800C4794_t unk8;
    char* limbName;
} D_800EB9B8_unk990;

typedef struct {
    func_800C1564_t unk0[2];
    short unk20;
    short unk22;
    short unk24;
    short unk26;
    short unk28;
    short unk2A;
    char unk2C;
    char unk2D;
    char unk2E;
    char unk2F;
    char unk30;
    char unk31;
    short unk32;
    int unk34;
    func_800C4794_t unk38;
    func_800C5798_t* unk40; /* the rows (vs_battle_setSphereTargets's argument, vs_battle_initTargetSelectionRow) */
    func_800C5798_t* unk44; /* the triggers (func_800C05EC's argument) */
    menuCircleVertex points[297]; /* the target shapes' points (8 bytes each); the PS1's 0x48..0x990 */
    D_800EB9B8_unk990 unk990[24];
    char spans[0x3800]; /* the map screen: 224 rows of 32 x-coordinates (span pairs) */
    int cellValues[81];
    char outlines[0x604];
    u_char cellKinds[81];
} D_800EB9B8_t;
