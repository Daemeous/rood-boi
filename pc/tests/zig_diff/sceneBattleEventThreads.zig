//! zig_diff spec: pc_battle_event_threads -> scene_submit.battleEventThreads (146C.c, scene_submit.inc).
//! Inputs: the event runner's state D_800F4C34 (0, 1..3, 4, 5, 255), the skip state at any value, the
//! script base D_800F4C30 (none, the buffer the threads point into, or another buffer far from it so the
//! offsets pass 8 bits and go negative), four thread pointers (none or inside the script buffer), the
//! byte under each at any value (the opcode, above 127 too) and an opcode table of distinct words.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").battleEventThreads));

pub const classes = [_][:0]const u8{ "idle", "loading", "no-threads", "threads", "no-base" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F4C34().* = ctx.edge(u8, &.{ 0, 1, 2, 3, 4, 4, 4, 4, 5, 255 });
    v.g.D_800F4C2C().* = ctx.pick(u8, &.{ 0, 1, 2, 3, 255 });
    const script = ctx.allocBytes(512, 16);
    const far = ctx.allocBytes(4096, 16);
    v.g.D_800F4C30().* = switch (ctx.random.uintLessThan(u32, 6)) {
        0 => null,
        1 => @ptrCast(far),
        else => @ptrCast(script),
    };
    for (0..4) |i| {
        v.g.D_800F4C38()[i] = if (ctx.chance(4)) null else @ptrCast(script + ctx.random.uintLessThan(usize, 512));
    }
    for (v.g._opcodeFunctionTable(), 0..) |*slot, k| slot.* = @ptrFromInt(0x40_0000 + k * 16);
    return .{ ctx.alloc([2]c_int), ctx.alloc([4]c_int), ctx.alloc([4]c_int), ctx.alloc([4]?*anyopaque) };
}

pub fn classify(args: Args) usize {
    _ = args;
    const state = v.g.D_800F4C34().*;
    if (state == 0) return 0;
    if (state != 4) return 1;
    var any = false;
    for (0..4) |i| {
        if (v.g.D_800F4C38()[i] != null) any = true;
    }
    if (!any) return 2;
    return if (v.g.D_800F4C30().* == null) 4 else 3;
}
