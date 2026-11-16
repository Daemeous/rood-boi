#include "common.h"
#include "146C.h"
#include "30DB0.h"
#include "58578.h"
#include "573B8.h"
#include "5BF94.h"
#include "target_block.h"
#include "../../SLUS_010.40/main.h"
#include <memory.h>
#include <stddef.h>
#include <inline_c.h>
#include "pc/src/psx_types.h"

typedef struct {
    u_short unk0[3];
    u_char unk6;
    u_char unk7;
    u_char unk8;
    u_char unk9;
    signed char unkA;
    char unkB[0xD];
} func_800C4650_t;

extern D_800EB9B8_t* vs_battle_targetSphere;

void func_800C4650(func_800C4650_t* arg0, int arg1);

int vs_battle_mapStickDeadZone(int arg0);

int func_800C4734(void);

