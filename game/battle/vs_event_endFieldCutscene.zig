//! The battle-state reset that runs when a battle ends (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_event_endFieldCutscene (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Clears the battle's view, actor and sound state; leaves through vs_event_startPreparedCutscene when the battle is ending.
pub fn vs_event_endFieldCutscene() callconv(.c) void {
    c.vs_field_setMovingPlatformsPaused(0);
    c.vs_battle_setActorMarkersHidden(0);
    v.g._firstPersonViewEnabled().* = 0;

    var i: c_int = 0;
    while (i < 16) : (i +%= 1) {
        const slot = v.g.vs_battle_actors()[@intCast(i)];
        if (slot) |actor| {
            if (actor.unk8 == 0x10) {
                actor.unk8 = 0x40;
            }
        }
    }

    _ = c.vs_battle_aiRebuildActorActionPlans(null);
    c.vs_battle_aiUpdateRouteGateAvailability();
    if (c.vs_event_evaluateRoomEventTriggers(0xA, 0, 0, 0) == 1 or
        (v.g.vs_state_sceneTimer().mode != 0 and c.vs_event_evaluateRoomEventTriggers(0x10, 0, 0, 0) == 1))
    {
        c.vs_event_startPreparedCutscene();
        return;
    }

    // uninitialised in the original: the rest of the struct is not written before the call
    var sp10: v.D_800F4538_unk64 = .{ .unk0 = 0, .unk1 = 0, .unk2 = 0, .unk3 = 0 };
    sp10.unk0 = 0;
    _ = c.vs_actors_setActorSoundOverrides(-1, psx.cptr(c.D_800F4538_unk64, &sp10));

    var j: c_int = 0;
    while (j < 16) : (j +%= 1) {
        _ = c.func_8009D7E8(j, 0);
        _ = c.vs_actors_setActorTint(j, 0);
    }

    c.vs_field_restoreRoomEffectSoundVolume();
    c.vs_battle_setFarClip(0x1000, 1);
    c.vs_battle_setCameraRoll(0);
    _ = c.func_8007BD8C(0);

    const sound = v.g.vs_main_soundData();
    if (sound.unk4 == 0xFFFF) {
        if (v.g._zoneContext().musicSlot != 0) {
            _ = c.vs_audio_playMusicSlot(v.g._zoneContext().musicSlot, 0x7F, 0);
        }
    } else if (v.g._zoneContext().musicSlot != 0 and
        sound.unk4 == psx.at(&sound.unk14, v.g._zoneContext().musicSlot -% 1).*)
    {
        _ = c.vs_audio_fadeMusicSlotVolume(v.g._zoneContext().musicSlot, 0x7F, 0x3C);
    }
    c.func_8006F5CC();
}
