//! Moves an effect point along a line (src/BATTLE/BATTLE.PRG/5BF94.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D01E4 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// The point between its saved position and its endpoint (the direction by mode bit 7) at its frame: linear, eased or by a curve (mode bits 5..6).
pub fn func_800D01E4(mode_arg: ?*v.func_800D0548_arg0, point: ?*v.func_800D0548_arg1) callconv(.c) void {
    const a = mode_arg.?;
    const b = point.?;
    var start: v.SVECTOR = undefined;
    var end: v.SVECTOR = undefined;
    if ((a.mode & 0x80) != 0) {
        start = b.saved;
        end.vx = psx.conv(i16, b.endpoint.vx);
        end.vy = psx.conv(i16, b.endpoint.vy);
        end.vz = psx.conv(i16, b.endpoint.vz);
    } else {
        end = b.saved;
        start.vx = psx.conv(i16, b.endpoint.vx);
        start.vy = psx.conv(i16, b.endpoint.vy);
        start.vz = psx.conv(i16, b.endpoint.vz);
    }
    var result = std.mem.zeroes(v.SVECTOR); // mode 3 leaves it uninitialised in the C
    switch ((a.mode >> 5) & 3) {
        0 => c.func_800D1778(@ptrCast(&start), @ptrCast(&end), @as(c_int, a.limit) + 1, b.frame, @ptrCast(&result)),
        1 => c.func_800D1718(@ptrCast(&start), @ptrCast(&end), @as(c_int, a.limit) + 1, b.frame, @ptrCast(&result)),
        2 => c.func_800D1778(@ptrCast(&start), @ptrCast(&end), 128, c.vs_battle_sampleCurve(@as(c_int, a.curve) + 1, b.frame), @ptrCast(&result)),
        else => {},
    }
    b.position.vx = result.vx;
    b.position.vy = result.vy;
    b.position.vz = result.vz;
}
