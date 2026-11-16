#include "common.h"
#include "../MAINMENU.PRG/224.h"
#include "../../BATTLE/BATTLE.PRG/146C.h"
#include "../../BATTLE/BATTLE.PRG/2842C.h"
#include "../../BATTLE/BATTLE.PRG/30DB0.h"
#include "../../BATTLE/BATTLE.PRG/44F14.h"
#include "../../BATTLE/BATTLE.PRG/5BF94.h"

int func_80109320(int);
void _populateDataSlot(int wepId, int material);

int _loadObjectData(int category, int wepId, int material)
{
    int _[6] __attribute__((unused));
    int i;
    D_800F4538_t* temp_s1 = vs_actors_actorRecords[1];

    switch (vs_battle_loadObjectDataState) {
    case 0:
        if (category == 0) {

            vs_content_releaseWeaponModel(1);

            if ((i = wepId) && func_80109320(temp_s1->unk5B1)) {
                vs_content_releaseWeaponModel(0);
                vs_menu_frameworkQueueAshleyWeaponAnimations(10);
            }
        } else {
            if (category != temp_s1->unk5B1) {
                vs_menu_frameworkQueueAshleyWeaponAnimations(category);
            }

            vs_content_releaseWeaponModel(0);

            if (vs_battle_unusedGlobal2 && func_80109320(category)) {
                vs_content_releaseWeaponModel(1);
            }
        }

        _populateDataSlot(wepId, material);
        vs_actors_setActorWeaponDrawn(1, 0, 1);

        ++vs_battle_loadObjectDataState;
        break;

    case 1:
        if ((vs_battle_processObjectDataQueue() == 0) && (temp_s1->animationId == 1)) {
            vs_menu_frameworkResetAshleyEquipmentPose();

            ++vs_battle_loadObjectDataState;
        }
        break;

    case 2:
        vs_menu_frameworkReleaseActorPreview();
        vs_menu_frameworkCloneActorPreview(0);
        vs_menu_frameworkResetActorPreviewPose();

        temp_s1->unk0.weaponDrawn = 0;

        vs_actors_playActorAnimation(1, 1, 0, 0);
        func_800AFA28(temp_s1, &temp_s1->unkC54, 1);
        vs_main_memcpy(&temp_s1->unk704, &temp_s1->unkC54, sizeof temp_s1->unk704);

        for (i = 0; i < 2; ++i) {
            if (vs_battle_unusedGlobal1[i] != NULL) {
                vs_battle_unusedGlobal1[i]->opaque11 = 0;
            }
        }

        vs_actors_setActorWeaponDrawn(1, 1, 1);

        ++vs_battle_loadObjectDataState;
        break;

    case 3:
        if (temp_s1->animationId == 101) {
            vs_battle_loadObjectDataState = 0;
            return 0;
        }

        break;
    }
    return 1;
}

int func_80109320(int category)
{
    if (category < 3) {
        return 0;
    }

    if (category == 8) {
        return 1;
    }

    return category & 1;
}

void _populateDataSlot(int wepId, int material)
{
    vs_battle_objectData objData;
    int index;
    int var_a3;

    if (wepId == 0) {
        return;
    }

    index = 0;

    if (wepId < 96) {
        var_a3 = 240;
    } else {
        index = 1;
        var_a3 = 241;
    }

    objData.dataType = 3;
    objData.index = index;
    objData.modelId = wepId;
    objData.destination = vs_battle_characterState->unk48[index];
    objData.actorId = 0;
    objData.unk11 = var_a3;
    objData.material = material;

    vs_battle_populateDataSlot(&objData);
}
