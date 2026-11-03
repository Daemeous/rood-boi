//! zig_diff spec: func_800E6828 -> pc.merge_actor_flags.mergeActorFlags (D2: pc/src/battle_helpers.c).
//! Inputs: a random-filled actor block whose flag bytes at 0x224/0x225 are
//! chosen so that the merged flags (low | high) have bits 4-5 set, only bit 3
//! set, or neither, with the high byte often nonzero (it merges into the low
//! one) and the output bytes at 0x15E/0x15F/0x125 pre-filled randomly.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").mergeActorFlags));

pub const classes = [_][:0]const u8{ "bits-4-5", "bit-3-only", "neither" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x300, 16);
    for (block[0..0x300]) |*byte| byte.* = ctx.int(u8);
    block[0x224] = ctx.pick(u8, &.{ 0, 8, 0x10, 0x20, 0x30, 0x18, 0xC7, 0xFF });
    block[0x225] = ctx.pick(u8, &.{ 0, 0, 8, 0x10, 0x20, 0x30, 0x80, 0xFF });
    return .{block};
}

pub fn classify(args: Args) usize {
    const merged = args[0][0x224] | args[0][0x225];
    if (merged & 0x30 != 0) return 0;
    if (merged & 8 != 0) return 1;
    return 2;
}
