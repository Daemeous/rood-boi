//! Builds a hit request for a hit entity and passes it to func_800CF3F8
//! (src/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8006C4C4 (src/BATTLE/BATTLE.PRG/146C.c)
/// Fills a request from the current battle actor and hit entity `arg1` against `targetLimb`, then hands it to func_800CF3F8.
pub fn func_8006C4C4(arg0: c_int, arg1: ?*v._hitEntity_t, targetLimb: c_int) callconv(.c) void {
    const base = v.g.D_800F19CC().*.?;
    const temp = &base.unk854[base.unk0 & 3];

    // Zeroed first: the fields the request does not set reach func_800CF3F8 as zero.
    var sp10 = std.mem.zeroes(v.func_8006C4C4_t);
    // func_800CF3F8 copies a whole func_800CF0E8_t from it: the C's ROOD_PC `unkE4` makes the request that big.
    comptime std.debug.assert(@sizeOf(v.func_8006C4C4_t) >= @sizeOf(v.func_800CF0E8_t));

    sp10.unk0 = psx.conv(i16, arg0);
    sp10.unk2 = 1;
    sp10.unk3 = 0;
    sp10.unk4 = 4;
    sp10.targetActor = temp.unk4.unk0.targetActor;
    sp10.unkA = 0;

    const hit = arg1.?;
    if (hit.unk40 == 0) {
        sp10.unk10 = 4;
        sp10.unk14.targetActor = hit.unk0.targetActor;
        sp10.unk14.targetLimb = psx.conv(i8, targetLimb);
        sp10.unk14.hitThreshold = psx.conv(u8, c.func_800A152C(
            @as(c_int, hit.unk0.targetActor),
            @as(c_int, hit.unk0.targetLimb),
            2,
        ));
    } else {
        sp10.unk10 = 5;
        sp10.unk14 = hit.unk0;
    }

    sp10.unk12 = psx.conv(i16, temp.unk2);

    _ = c.func_800CF3F8(@ptrCast(&sp10), 0);
}
