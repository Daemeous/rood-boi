//! Starts a battle action: resets the action record for the target actor and clears the other actors' pending flags (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_beginActorActionSelection (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets up the action record for target actor `arg0` with action index `arg1` and prepares the battle for it.
pub fn vs_battle_beginActorActionSelection(arg0: c_int, arg1: c_ushort) callconv(.c) void {
    v.g.D_800F196C().* = 2;
    c.vs_main_bzero(&v.g.vs_battle_actionState().*.?.unk8, 0x84C);
    v.g.vs_battle_actionState().*.?.unk8.primaryTarget.unk40 = 0;
    v.g.vs_battle_actionState().*.?.unk8.primaryTarget.target.targetActor = psx.conv(u8, arg0);
    v.g.vs_battle_actionState().*.?.unk8.actionIndex = arg1;
    c.vs_field_storePreferredActionCameraDistance();
    c.func_80088CA0();
    if (arg0 != 0) {
        v.g.vs_battle_actionState().*.?.unk2984 = 0;
    } else {
        v.g.vs_battle_actionState().*.?.unk2984 = 1;
    }
    v.g.vs_battle_actionState().*.?.unk298C = 0xFF;
    v.g.vs_battle_actionState().*.?.unk298D = 0xFF;

    // vs_battle_actors[arg0]: no bounds check, as in the C; a null actor traps where the C would fault.
    const temp_a0: *v.vs_battle_actor = psx.at(v.g.vs_battle_actors(), arg0).*.?;
    v.g.vs_battle_actionState().*.?.previews[0].unk9 = temp_a0.unk10.u8[0];
    v.g.vs_battle_actionState().*.?.previews[0].unkA = temp_a0.unk10.u8[2];
    v.g.vs_battle_actionState().*.?.unk8.targetCount = 0;
    v.g.vs_battle_actionState().*.?.unk298E = 0;
    v.g.vs_battle_actionState().*.?.unk4 = 0;
    c.func_8009E5C4(@as(c_int, v.g.vs_battle_actionState().*.?.unk8.primaryTarget.target.targetActor));

    if (v.g.vs_battle_actionState().*.?.unk2984 != 0) {
        c.func_800C031C();
        c.vs_render_transformRoomPalettes(0xD, 4, -4, -4, 0);
    }

    c.vs_actors_freezeAllActorAnimations();

    var i: c_int = 0;
    while (i < 16) : (i +%= 1) {
        if (i != @as(c_int, v.g.vs_battle_actionState().*.?.unk8.primaryTarget.target.targetActor)) {
            const temp_v0: c_uint = c.vs_actors_actorStateFlags(i);
            if ((temp_v0 & 4) != 0) {
                // The flags argument is an int in the callee's prototype: the C converts the u_int.
                c.vs_actors_setActorDisplayFlags(i, psx.conv(c_int, temp_v0 & ~@as(c_uint, 4)));
            }
        }
    }

    if (arg0 == 0) {
        _ = c.vs_field_setMinimapVisible(0);
    }
    c.vs_field_setRoomAnimationsPaused(1);
    c.vs_field_setMovingPlatformsPaused(1);
    if (v.g.vs_battle_actionState().*.?.unk2984 != 0) {
        c.vs_battle_setActorMarkersHidden(1);
    }
    c.vs_battle_initializeActionTargeting(@as(c_int, v.g.vs_battle_actionState().*.?.unk8.actionIndex));
    c.vs_main_setClutState(0);
}
