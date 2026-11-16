#include "57BC.h"
#include "592C.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/146C.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/2842C.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/30DB0.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/40564.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/5BF94.h"
#include "pc/src/game/MENU/MAINMENU.PRG/224.h"

int vs_menu9_loadZud(int zudId)
{
    int state = vs_battle_loadObjectDataState;

    switch (state) {
    case 0:
        vs_menu9_loadZudFile(zudId);

        ++vs_battle_loadObjectDataState;
        break;

    case 1:
        if (vs_menu9_parseZudFile() != 1) {
            ++vs_battle_loadObjectDataState;
        }

        break;

    case 2:
        vs_menu_frameworkResetActorPreviewPose();

        vs_actors_actorRecords[1]->unk0.weaponDrawn = 1;

        vs_actors_playActorAnimation(1, 1, 0, 0);

        vs_battle_loadObjectDataState = 0;
        return 0;
    }

    return 1;
}

void vs_menu9_freeZud(void)
{
    vs_content_releaseActorModel(1);
    vs_content_releaseActorModel(16);
    vs_menu9_freeZudFile();
}

void func_801080C8(void)
{
    D_800F4538_t* p = vs_actors_actorRecords[1];

    p->animationId = 33;
    p->unk0.weaponDrawn = 0;

    vs_actors_initializeActorAnimationPose(p, &p->unk704);
    vs_actors_setActorDisplayFlags(1, 3);
    func_8009F940(1, 0, NULL, NULL);
}
