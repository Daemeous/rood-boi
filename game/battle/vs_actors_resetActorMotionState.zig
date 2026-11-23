//! Resets a battle entity slot (vs_actors_actorRecords) to its spawn state (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_actors_resetActorMotionState (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Resets the entity in slot `index` of vs_actors_actorRecords to its spawn state, if the slot is in use.
pub fn vs_actors_resetActorMotionState(index: c_int, _: c_int) callconv(.c) void {
    if (psx.get(psx.at(v.g.vs_actors_actorRecords(), index).*)) |slot| {
        slot.unk0.scaleX = 0x1000;
        slot.unk0.scaleY = 0x1000;
        slot.unk0.scaleZ = 0x1000;
        slot.unk17FC = -2;
        slot.unk0.unk11 = 0;
        slot.scaleFrames = 0;
        slot.unk0.turnFrames = 0;
        @as(*align(1) i32, @ptrCast(&slot.unk0.pitch)).* = 0; // *(int*)&unk24: pitch and facing
        @as(*align(1) i32, @ptrCast(&slot.unk0.roll)).* = 0; // *(int*)&unk28: roll and unk2A
        slot.unk0.translationFrames = 0;
        slot.unk0.unk16 = 0;
        slot.unk0.unk14 = 0;
        psx.setBits(&slot.unk0, "unkA_0", 0);
        psx.setBits(slot, "unk5AC_0", 0);
        psx.setBits(&slot.unk0, "unkB_0", 0);
        psx.setBits(&slot.unk0, "unkB_4", 0);
        _ = c.vs_actors_headTrackingSpeed(@ptrCast(slot));
        if (v.g.D_800F2450()[v.g.vs_actors_sharedSequenceBankIndex().*] != 0) {
            _ = c.vs_actors_textureFrameMove(@ptrCast(slot), 0, 0);
            _ = c.vs_actors_textureFrameMove(@ptrCast(slot), 1, 0);
        }
        slot.unk1802[0] = 0;
        psx.setBits(&slot.unk180C[0], "unk0_0", 0);
        psx.setBits(slot, "unk5AC_21", 0);
        psx.setBits(&slot.unk180C[1], "unk0_0", 0);
        psx.setBits(slot, "unk5AC_22", 0);
        _ = c.vs_actors_setActorTint(index, null);
        if (slot.companionSlot == 1) {
            slot.companionSlot = 0;
        }
        psx.setBits(&slot.unk0, "unk9_7", 1);
        var sp10: v.D_800F4538_unk64 = undefined;
        sp10.unk0 = 0;
        _ = c.vs_actors_setActorSoundOverrides(index, psx.cptr(c.D_800F4538_unk64, &sp10));
        slot.unk5BE = 4;
        slot.unk17FF = 0;
        slot.unk0.palettePulse.unk0 = 0;
        slot.unk1860 = 0;
        psx.setBits(&slot.unk0, "unkC_0", 0);
        psx.setBits(&slot.unk0, "unkC_4", 0);
        psx.setBits(&slot.unk0, "unkA_3", 0);
        psx.setBits(&slot.unk0, "unk9_6", 0);
        psx.setBits(slot, "unk5AC_3", 0);
        psx.setBits(&slot.unk0, "unk9_0", 0);
        var var_a2: c_int = 0x62;
        if (index == 0) {
            var_a2 = 1;
        }
        _ = c.vs_actors_playActorAnimation(index, var_a2, 0, 0);
        slot.queuedAnimation = 0;
        slot.unk6EC = 0;
        slot.unk6EE = 0;
        slot.unk6EF = 0;
        slot.unk1846 = 0;
        psx.setBits(slot, "unk5AC_24", 0);
        psx.setBits(&slot.unk0, "skip", 1);
        psx.setBits(&slot.unk0, "visible", 1);
        psx.setBits(&slot.unk0, "freeze", 1);
        psx.setBits(slot, "unk5AC_15", 0);
        const s0 = &slot.unk1864;
        _ = c.memset(s0, @as(c_int, 0), @as(usize, @sizeOf(v.D_800F4538_unk1864)));
        s0.radiusExpandRate = slot.shadowRadiusExpandRate;
        s0.radiusShrinkRate = slot.shadowRadiusShrinkRate;
        _ = c.memset(&slot.unk1848, @as(c_int, 0), @as(usize, @sizeOf(v.D_800F4538_unk1848)));
    }
}
