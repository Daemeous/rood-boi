//! zig_diff spec: _loadMpdClearedScriptDoorSections -> load_mpd_cleared_script_door_sections.loadMpdClearedScriptDoorSections (4A0A8.c).
//! Inputs: three arena sections with lengths 0 or 1..512 (memcpy runs as
//! libc's into the heap blocks the vs_main_allocHeap stub hands out, and into
//! D_800F4C30, an arena buffer), the start state 0 or not, and D_800F4BE2 at
//! 1 (the early return), 3 (the reset) or another value.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").loadMpdClearedScriptDoorSections));

pub const classes = [_][:0]const u8{ "pending", "reset", "plain" };

var heap: [2][*]u8 = undefined;

fn len(ctx: *zd.Ctx) u16 {
    return if (ctx.chance(3)) 0 else ctx.pick(u16, &.{ 1, 4, 512, 37 }) % 513;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    heap = .{ ctx.allocBytes(512, 16), ctx.allocBytes(512, 16) };
    v.g.D_800F4C30().* = @ptrCast(ctx.allocBytes(512, 16));
    v.g.D_800F4BE2().* = ctx.pick(i16, &.{ 1, 3, 0, 2 });
    const cleared = ctx.allocBytes(512, 4);
    const script = ctx.allocBytes(512, 4);
    const door = ctx.allocBytes(512, 4);
    const start: c_int = if (ctx.chance(2)) 0 else ctx.pick(c_int, &.{ 1, -1 });
    return .{ cleared, len(ctx), script, len(ctx), door, len(ctx), start };
}

pub fn classify(args: Args) usize {
    _ = args;
    return switch (v.g.D_800F4BE2().*) {
        1 => 0,
        3 => 1,
        else => 2,
    };
}

pub const stubs = struct {
    /// Each call a different block: the cleared section's, then the door's.
    pub fn vs_main_allocHeap(ctx: *zd.Ctx, size: c_uint) ?*anyopaque {
        _ = size;
        return @ptrCast(heap[ctx.value(u8) & 1]);
    }
};
