#pragma once
#include "../../SLUS_010.40/main.h"
#include "pc/src/psx_types.h"

typedef struct {
    u_char unk0;
    u_char unk1;
    u_short unk2;
    short unk4;
    short unk6;
    int unk8;
    psx_ptr(vs_main_CdQueueSlot) slot;
} D_800EB9B4_t;
PSX_ASSERT_SIZE(D_800EB9B4_t, 0x10);

#ifdef ROOD_PC
/* This overlay shares guest addresses with unrelated effect plugins. */
#define func_800F9800 pc_screff2_start
#define func_800F986C pc_screff2_tiles
#define func_800F9BC0 pc_screff2_set_warp
#define func_800F9BD8 pc_screff2_warp
#define func_800F9DE8 pc_screff2_set_border
#define func_800F9FB8 pc_screff2_border
#endif

void func_800F9800(int);
void func_800F986C(void);
void func_800F9BD8(void);
void func_800F9DE8(int);
void func_800F9FB8(void);

extern D_800EB9B4_t* D_800EB9B4;
