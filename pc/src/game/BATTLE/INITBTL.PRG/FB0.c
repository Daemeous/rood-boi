#include "pc/src/game/BATTLE/BATTLE.PRG/146C.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/30DB0.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/3A1A0.h"
#include <libgte.h>

extern char D_800E8F29;
extern char _loadShpState;
extern char D_800E8F2B;
extern char vs_actors_sharedSequenceBankIndex;
extern char _loadEtmState;
extern short vs_actors_textureSlotOrigins[][2];
extern char D_800E8FC0;
extern char D_800F2450[];
extern void* vs_actors_sharedSequenceData[]; /* 30DB0.c: heap buffer pointers (8 bytes each on LP64) */
extern void* D_800F4768;
extern char D_800F4B18;
extern char D_800F4B19;

void func_800FA7B0(void)
{
    VECTOR sp10;
    int temp_s0;
    int i;

    D_800E8FC0 = 0;

    for (i = 0; i < 16; ++i) {
        vs_battle_objectDataSlots[i].dataType = 0;
    }

    for (i = 0; i < 12; ++i) {
        short (*new_var)[2];
        vs_render_legacyActorTextureSlots[i].tpage =
            GetTPage(0, 0, vs_actors_textureSlotOrigins[i][0] << 6, (new_var = vs_actors_textureSlotOrigins)[i][1] << 8);
        vs_render_legacyActorTextureSlots[i].clut0 = GetClut(768, i + 228);
        vs_render_legacyActorTextureSlots[i].clut1 = GetClut(768, i + 246);
        vs_render_legacyActorTextureSlots[i].refCount = 0;
    }

    for (; i < 24; ++i) {
        short (*new_var)[2];
        vs_render_legacyActorTextureSlots[i].tpage =
            GetTPage(0, 0, vs_actors_textureSlotOrigins[i][0] << 6, (new_var = vs_actors_textureSlotOrigins)[i][1] << 8);
        vs_render_legacyActorTextureSlots[i].clut0 = GetClut(768, 229);
        vs_render_legacyActorTextureSlots[i].clut1 = GetClut(768, 247);
        vs_render_legacyActorTextureSlots[i].refCount = 0;
    }

    for (i = 0; i < 17; ++i) {
        vs_actors_actorRecords[i] = NULL;
    }

    D_800F4930 = vs_main_allocHeap(0x7800);
    D_800F4768 = vs_main_allocHeap(0x4800);

    for (i = 0; i < 20; ++i) {
        temp_s0 = (i / 2);
        vs_battle_wepTextures[i].tpage = GetTPage(1, 0, temp_s0 * 64, 0);
        vs_battle_wepTextures[i].clut0 = GetClut(((i & 1) * 48) + 928, temp_s0 + 228);
        vs_battle_wepTextures[i].refCount = 0;
    }

    for (i = 0; i < 20; ++i) {
        vs_battle_wepModels[i] = NULL;
    }

    for (i = 0; i < 2; ++i) {
        vs_battle_commonWepBuffs[i] =
            vs_main_allocHeap(sizeof *vs_battle_commonWepBuffs[i]);
    }

    for (i = 0; i < 16; ++i) {
        vs_field_gimmickInstances[i] = NULL;
    }

    vs_actors_sharedSequenceBankIndex = 0;
    D_800F2450[0] = 0;
    D_800F2450[1] = 0;
    D_800F2450[2] = 0;
    vs_actors_sharedSequenceData[0] = 0;
    vs_actors_sharedSequenceData[1] = 0;
    vs_actors_sharedSequenceData[2] = 0;
    D_800E8F2B = 0;
    _loadEtmState = 0;
    D_800E8FC0 = 0;
    D_800E8F29 = 0;
    vs_battle_loadObjectDataState = 0;
    _loadShpState = 0;
    vs_actors_sharedSequenceBankIndex = 0;
    sp10.vx = 0x1000;
    sp10.vy = 0x1000;
    sp10.vz = 0x1000;
    vs_actors_setRoomActorScale(&sp10);
    D_800F4B19 = 0;
    D_800F4B18 = 0;
}
