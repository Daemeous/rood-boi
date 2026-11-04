//! zig_diff spec: func_800DC2E0 -> pc.copy_candidate.copyCandidate (D2: pc/src/battle_helpers.c).
//! Inputs: a random-filled arena area holding a 20-byte source and a 20-byte
//! destination that never overlap (a partial overlap is compiler-defined),
//! or the same record as both (a copy onto itself changes nothing).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").copyCandidate));

pub const classes = [_][:0]const u8{ "distinct", "same-record" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const area = ctx.allocBytes(0x80, 16);
    for (area[0..0x80]) |*byte| byte.* = ctx.int(u8);
    const src: [*]align(1) c_uint = @ptrCast(area + 0x40);
    const dst: [*]align(1) c_uint = if (ctx.chance(5)) src else @ptrCast(area + 0x10);
    return .{ dst, src };
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[0] == args[1]);
}
