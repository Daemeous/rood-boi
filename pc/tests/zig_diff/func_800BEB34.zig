//! zig_diff spec: func_800BEB34 -> func_800BEB34.func_800BEB34 (4A0A8.c).
//! Inputs: the D_800F4BE2 selector (3, and neighbours and extremes, as i16),
//! and the two section pointers, each null or non-null. Non-null sections are
//! arena blocks or values with the sign bit set; the C only passes them on.
//! func_800BFD9C is a stub that may change the selector and both sections,
//! which the C reads back after the call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BEB34));

// Index = (selector == 3) * 4 + (cleared != null) * 2 + (door != null).
pub const classes = [_][:0]const u8{
    "sel-other-none",
    "sel-other-door",
    "sel-other-cleared",
    "sel-other-both",
    "sel-3-none",
    "sel-3-door",
    "sel-3-cleared",
    "sel-3-both",
};

fn sectionPtr(ctx: *zd.Ctx) ?*anyopaque {
    if (ctx.chance(2)) return null;
    if (ctx.chance(2)) return @ptrFromInt(ctx.edge(usize, &.{ 0x8000_0000, 0x7FFF_FFF0, 0x1F80_0000 }));
    return @ptrCast(ctx.allocBytes(16, 16));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const sel: i16 = if (ctx.chance(2)) 3 else ctx.pick(i16, &.{ 0, 2, 4, 1, -1, 32767, -32768 });
    v.g.D_800F4BE2().* = sel;
    v.g._mpdClearedSection().* = sectionPtr(ctx);
    v.g._mpdRoomDoorSection().* = sectionPtr(ctx);
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const sel3: usize = if (v.g.D_800F4BE2().* == 3) 4 else 0;
    const cleared: usize = if (v.g._mpdClearedSection().* != null) 2 else 0;
    const door: usize = if (v.g._mpdRoomDoorSection().* != null) 1 else 0;
    return sel3 + cleared + door;
}

pub const stubs = struct {
    // The real callee may change the selector and both sections.
    pub fn func_800BFD9C(ctx: *zd.Ctx) void {
        if (ctx.value(u1) == 1) v.g.D_800F4BE2().* = ctx.value(i16);
        if (ctx.value(u1) == 1) v.g._mpdClearedSection().* = @ptrFromInt(ctx.value(usize));
        if (ctx.value(u1) == 1) v.g._mpdRoomDoorSection().* = null;
    }
};
