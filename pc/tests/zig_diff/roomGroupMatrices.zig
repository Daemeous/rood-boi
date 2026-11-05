//! zig_diff spec: func_800983F8 -> pc.room_group_matrices.roomGroupMatrices (D2: pc/src/boot_sdk.c).
//! Inputs: a geometry block in the arena (a group count of 0..6, mostly 1..4, then
//! groups of 0x40 random bytes whose flag word is 0, 0x100 (skipped), 0x400
//! (camera-facing), 0x500 or any word) and the scratchpad's angle (word 0x5C) and
//! scale (word 0x64), edges or random. rsin, rcos, MTC2, MVMVA and MFC2 are
//! recording stubs returning drawn values; func_800985AC (a D2 neighbour) and the
//! libgte setters record their arguments (the stack vectors as a tag). A count
//! over 4096 (pc_unimplemented) would run past the 128 KB arena and is not
//! generated.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").roomGroupMatrices));

pub const classes = [_][:0]const u8{ "no-groups", "skipped-only", "plain", "facing" };

const flag_edges = [_]u32{ 0, 0x100, 0x400, 0x500, 0x200, 0x4ff, 0xfffffeff };

pub fn prepare(ctx: *zd.Ctx) Args {
    const count: u32 = if (ctx.chance(6)) 0 else ctx.range(u32, 1, if (ctx.chance(4)) 6 else 3);
    const block = ctx.allocBytes(4 + 0x40 * 6, 16);
    for (block[0 .. 4 + 0x40 * 6]) |*byte| byte.* = ctx.int(u8);
    const words: [*]align(1) u32 = @ptrCast(block);
    words[0] = count;
    for (0..count) |i| words[1 + i * 16] = ctx.pick(u32, &flag_edges);
    const scratch: [*]volatile c_int = @ptrFromInt(0x1f800000);
    scratch[0x5c / 4] = ctx.pick(c_int, &.{ 0, 0x400, 0x800, 0xc00, 0xfff, -1, 0x1000, 0x7fffffff });
    scratch[0x64 / 4] = ctx.pick(c_int, &.{ 0, 0x1000, -0x1000, 1, 0x7fffffff, -0x80000000 });
    return .{block};
}

pub fn classify(args: Args) usize {
    const words: [*]align(1) const u32 = @ptrCast(args[0].?);
    const count = words[0];
    if (count == 0) return 0;
    var plain = false;
    for (0..count) |i| {
        const flags = words[1 + i * 16];
        if (flags & 0x100 != 0) continue;
        if (flags & 0x400 != 0) return 3;
        plain = true;
    }
    return if (plain) 2 else 1;
}
