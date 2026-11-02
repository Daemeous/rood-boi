//! zig_diff spec: func_800CC204 -> func_800CC204.func_800CC204 (5BF94.c).
//! Inputs: an arena image with random line widths (0..320 and past it),
//! tiles (some 0) and ground colour (0 or not); the wipe position unkC at,
//! below or above 4096 (and its top bits set); its side unk3 0 or not;
//! widescreen (pc_gte_widescreen, the anchor hook a stub returning 0 or 1)
//! on or off. vs_battle_setSprite's stub returns one arena packet (the C
//! writes two of its words per sprite: the state hash at each call sees
//! them); the line work makes the spec slow, so fewer iterations.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CC204));

pub const classes = [_][:0]const u8{ "full", "growing", "shrinking", "widescreen" };

var recorded: c_int = 0;
var packet: ?[*]u32 = null;

pub const stubs = struct {
    pub fn pc_hw_anchor_recorded(ctx: *zd.Ctx, anchor: anytype) c_int {
        _ = ctx;
        _ = anchor;
        return recorded;
    }
    pub fn vs_battle_setSprite(ctx: *zd.Ctx, a: anytype, b: anytype, w: anytype, ot: anytype) [*c]u32 {
        _ = ctx;
        _ = a;
        _ = b;
        _ = w;
        _ = ot;
        return packet.?;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    packet = @ptrCast(@alignCast(ctx.allocBytes(32, 16)));
    const image = ctx.alloc(v.gim_t);
    const widths: [*]u16 = @ptrCast(&image.unk18);
    for (0..240) |i| widths[i] = ctx.pick(u16, &.{ 0, 1, 63, 64, 160, 320, 400 });
    const tiles: [*]align(1) u16 = @ptrFromInt(@intFromPtr(image) + 0x200);
    for (0..80) |i| tiles[i] = if (ctx.chance(3)) 0 else ctx.int(u16);
    image.unkE = ctx.pick(i16, &.{ 0, 0x7FFF, -1 });
    image.unkC = ctx.pick(i16, &.{ 4096, 4096, 0, 1000, 4095, 4097, 8191, 0x4000 | 4096, -1 });
    image.unk3 = ctx.pick(u8, &.{ 0, 1, 0x80 });
    v.g.pc_gte_widescreen().* = ctx.edge(i32, &.{ 0, 1 });
    recorded = ctx.edge(c_int, &.{ 0, 1, 1 });
    return .{ image, ctx.int(c_int), @ptrCast(@alignCast(ctx.allocBytes(16, 16))) };
}

pub fn classify(args: Args) usize {
    const x = args[0].?.unkC & 0x3FFF;
    if (x == 4096) return 0;
    if (v.g.pc_gte_widescreen().* != 0 and recorded != 0) return 3;
    return if (x < 4096) 1 else 2;
}
