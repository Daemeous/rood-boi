#include "9258.h"
#include "pc/src/game/MENU/MAINMENU.PRG/224.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/146C.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/30DB0.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/3A1A0.h"

int _loadObject(int arg0)
{
    int _[6] __attribute__((unused));
    D_800F4538_t* new_var = vs_actors_actorRecords[0];

    if (arg0 == 1) {
        vs_content_releaseWeaponModel(1);
        vs_menu_frameworkResetAshleyEquipmentPose();
        return 0;
    }

    switch (vs_battle_loadObjectDataState) {
    case 0:
        if (new_var->unk5B1 == 10) {
            return 0;
        }

        vs_content_releaseWeaponModel(0);
        vs_menu_frameworkQueueAshleyWeaponAnimations(10);

        ++vs_battle_loadObjectDataState;
        break;

    case 1:
        if (vs_battle_processObjectDataQueue() == 0) {

            vs_menu_frameworkResetAshleyEquipmentPose();

            vs_battle_loadObjectDataState = 0;
            return 0;
        }

        break;
    }

    return 1;
}
