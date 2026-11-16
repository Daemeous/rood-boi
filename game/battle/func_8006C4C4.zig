//! Builds a hit request for a hit entity and passes it to vs_effects_beginSecondaryEffects
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8006C4C4 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Fills a request from the current battle actor and hit entity `arg1` against `targetLimb`, then hands it to vs_effects_beginSecondaryEffects.
pub fn func_8006C4C4(arg0: c_int, arg1: ?*v._hitEntity_t, targetLimb: c_int) callconv(.c) void {
    const base = v.g.vs_battle_actionState().*.?;
    const temp = &base.queue[base.unk0 & 3];

    // Zeroed first: the fields the request does not set reach vs_effects_beginSecondaryEffects as zero.
    var sp10 = std.mem.zeroes(v.func_8006C4C4_t);
    // vs_effects_beginSecondaryEffects copies a whole func_800CF0E8_t from it: the C's ROOD_PC `unkE4` makes the request that big.
    comptime std.debug.assert(@sizeOf(v.func_8006C4C4_t) >= @sizeOf(v.func_800CF0E8_t));

    sp10.unk0 = psx.conv(i16, arg0);
    sp10.unk2 = 1;
    sp10.unk3 = 0;
    sp10.unk4 = 4;
    sp10.targetActor = temp.primaryTarget.target.targetActor;
    sp10.unkA = 0;

    const hit = arg1.?;
    if (hit.unk40 == 0) {
        sp10.unk10 = 4;
        sp10.unk14.targetActor = hit.target.targetActor;
        sp10.unk14.targetLimb = psx.conv(i8, targetLimb);
        sp10.unk14.hitThreshold = psx.conv(u8, c.func_800A152C(
            @as(c_int, hit.target.targetActor),
            @as(c_int, hit.target.targetLimb),
            2,
        ));
    } else {
        sp10.unk10 = 5;
        sp10.unk14 = hit.target;
    }

    sp10.unk12 = psx.conv(i16, temp.unk2);

    _ = c.vs_effects_beginSecondaryEffects(@ptrCast(&sp10), 0);
}
