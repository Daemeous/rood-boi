//! Starts an actor's move over a distance in a direction, or stops it when the distance is zero (src/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A48CC (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Sets actor `index` moving over `distance` in `direction` (zero stops it), unless it is busy or already in a sequence.
pub fn func_800A48CC(index: c_int, direction: c_int, distance: c_int) callconv(.c) void {
    // The C indexes D_800F4538 without a bounds check and dereferences the entry unguarded.
    const actor = psx.at(v.g.D_800F4538(), index).*.?;
    const flags = psx.byteAt(v.vs_battleMovementModeFlags, actor, 0x5B0);

    if (psx.getBits(actor, "unk5AC_0") != 0) return;
    if (psx.getBits(&actor.unk0, "unkA_3") != 0) return;
    if (psx.getBits(&actor.unk0, "unkA_0") != 0) {
        c.func_800B64A8(index, direction, distance);
        return;
    }

    if (distance == 0) {
        psx.setBits(flags, "fast", 0);
        psx.setBits(flags, "mode", 0);
        _ = c.func_800A0204(index, 0x2F, 0, 4);
        actor.unk0.unk11 = 6;
        psx.setBits(&actor.unk0, "unkA_0", 2);
    } else {
        psx.setBits(flags, "fast", 0);
        psx.setBits(flags, "mode", 1);
        if (@divTrunc(distance, 4096) >= @as(c_int, actor.unk5B9)) {
            psx.setBits(flags, "mode", 2);
            psx.setBits(flags, "fast", 1);
        }
        actor.unk0.unk11 = 0;
        psx.setBits(&actor.unk0, "unkA_0", 1);
    }

    actor.unk1848.unk0 = 7;
    actor.unk1848.unk4 = psx.conv(u16, direction);
    actor.unk1848.unk8 = psx.conv(u32, distance);
}
