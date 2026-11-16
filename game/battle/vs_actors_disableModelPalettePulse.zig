//! Clears bits of the 0x64 status byte of a battle slot's entity, or the whole byte (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: vs_actors_disableModelPalettePulse (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Clears bit `bit` of the 0x64 status byte of slot `slot`'s entity (the whole byte for -1), falling back to the second table.
pub fn vs_actors_disableModelPalettePulse(slot: c_int, bit: c_int) callconv(.c) c_int {
    const temp_v0: ?*v.D_800F4538_t = psx.at(v.g.vs_actors_actorRecords(), slot).*;

    var a0: *u8 = undefined;
    if (temp_v0) |actor| {
        a0 = psx.fieldAt(u8, &actor.unk0.unk64.unk0, 0);
    } else {
        const other: ?*v.D_800F45E0_t = psx.at(v.g.vs_field_gimmickInstances(), slot).*;
        const o = other orelse return -1;
        a0 = psx.fieldAt(u8, &o.unk64.unk0, 0);
    }

    if (bit == -1) {
        a0.* = 0;
    } else {
        // Unsigned compare: a negative bit is above 7 and returns -2, as in the C.
        if (psx.conv(c_uint, bit) > 7) return -2;
        // bit is 0..7 here, so 7 - bit is 0..7 and the shift is in range.
        const v1: c_int = (@as(c_int, 1) << @intCast(7 - bit)) ^ 0xFF;
        a0.* &= psx.conv(u8, v1);
    }
    return 0;
}
