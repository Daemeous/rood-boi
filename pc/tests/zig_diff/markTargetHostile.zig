//! zig_diff spec: func_800DC784 -> pc.mark_target_hostile.markTargetHostile (D2: pc/src/battle_helpers.c).
//! Inputs: a random-filled state block, an actor id 0..16, and for the word
//! at 0x1A4 + id * 4 the classification in bits 6-7 each of 0..3 (3 means
//! the actor already is a target: no reaction). The stubs record rand and the
//! two reaction calls; the byte at 0x18 and the rewritten word are compared.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").markTargetHostile));

pub const classes = [_][:0]const u8{ "already-3", "reacts" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x300, 16);
    for (state[0..0x300]) |*byte| byte.* = ctx.int(u8);
    const id = ctx.range(c_int, 0, 16);
    const at = 0x1a4 + @as(usize, @intCast(id)) * 4;
    const flags = std.mem.readInt(u32, state[at..][0..4], .little);
    const level = ctx.range(u32, 0, 3);
    std.mem.writeInt(u32, state[at..][0..4], (flags & ~@as(u32, 0xc0)) | (level << 6), .little);
    return .{ state, id };
}

pub fn classify(args: Args) usize {
    const at = 0x1a4 + @as(usize, @intCast(args[1])) * 4;
    const flags = std.mem.readInt(u32, args[0][at..][0..4], .little);
    return @intFromBool((flags >> 6) & 3 < 3);
}
