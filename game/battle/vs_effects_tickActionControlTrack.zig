//! The battle queue step: runs the handler for a queued entry's kind when its
//! tag matches the current state, then retires the entry (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_tickActionControlTrack (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Dispatches queue entry `arg1` by its kind when its tag matches `arg0`'s, then retires it through vs_effects_advanceTimelineKey.
pub fn vs_effects_tickActionControlTrack(arg0: ?*v.D_800F53B8_t, arg1: ?*v.func_800D5780_t) callconv(.c) c_int {
    // The entry pointer is a u32* in the PS1 layout: the offset is scaled by 4 (pointer minus int).
    const entry_base: u32 = psx.addr(psx.get(arg1.?.unk0).?);
    const neg: c_int = -%@as(c_int, arg1.?.unk6);
    const entry_addr: u32 = entry_base -% @as(u32, @bitCast(neg)) *% 4;
    const entry: *align(1) v.func_800D6894_t = @ptrFromInt(@as(usize, entry_addr));

    const expected = psx.get(arg0.?.unkD1C.unk30).?;
    if (@as(c_int, entry.unk0) & 0x1FF == @as(c_int, expected.unk2)) {
        const a1: u8 = entry.unk2;
        const a2: u8 = entry.unk3;

        switch (@as(c_int, entry.unk0) >> 9) {
            0 => c.func_800D6388(@ptrCast(arg0)),
            1 => c.func_800D63D0(@ptrCast(arg0)),
            2 => c.vs_effects_targetRoomTile(@ptrCast(arg0)),
            3 => c.vs_effects_spawnTransformWeaponTrail(@ptrCast(arg0), a1, a2),
            4 => c.vs_effects_fadeActionWeaponTrails(),
            5 => c.vs_effects_resumeTransformMotion(@ptrCast(arg0), a1),
            6 => c.vs_effects_holdTimelineFrame(@ptrCast(arg0)),
            7 => c.func_800D6554(),
            8 => c.func_800D6574(@ptrCast(arg0)),
            9 => c.vs_effects_invalidateTransformAttachments(@ptrCast(arg0), a1),
            10 => c.vs_effects_sourceWeaponBrightness(@ptrCast(arg0), a1),
            11 => c.vs_effects_sourceReactionSound(@ptrCast(arg0), a1),
            12 => c.func_800D66CC(@ptrCast(arg0)),
            13 => c.vs_effects_focusCameraOnEffectTransform(@ptrCast(arg0), a1, a2),
            14 => c.vs_effects_restoreActionEffectCamera(@ptrCast(arg0), a1),
            15 => c.func_800D6860(@ptrCast(arg0)),
            else => {},
        }

        return c.vs_effects_advanceTimelineKey(@ptrCast(arg1));
    }

    return 1;
}
