//! zig_diff spec: func_800CC600 -> func_800CC600.func_800CC600 (5BF94.c).
//! Inputs: an arena image of 0..3 columns and 0..4 tile rows (tiles some
//! empty), its brightness 128 (the plain image: func_800CBBCC) or below,
//! its palette and page bits random; the frame buffer 0 or 1.
//! vs_battle_setSprite's stub returns one arena packet (its two words per
//! sprite: the state hash at each call sees them); the line work makes the
//! spec slow, so fewer iterations.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CC600));

pub const classes = [_][:0]const u8{ "plain", "blend" };

var packet: ?[*]u32 = null;

pub const stubs = struct {
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
    const dims: *[2]u8 = @ptrCast(&image.unk4);
    dims[0] = ctx.range(u8, 0, 3);
    dims[1] = ctx.range(u8, 0, 4);
    const tiles: [*]align(1) u16 = @ptrFromInt(@intFromPtr(image) + 0x200);
    for (0..12) |i| tiles[i] = if (ctx.chance(3)) 0 else ctx.int(u16);
    image.unk3 = ctx.pick(u8, &.{ 128, 0, 64, 127 });
    v.g.vs_main_frameBuf().* = ctx.edge(i32, &.{ 0, 1 });
    return .{ image, ctx.int(c_int), @ptrCast(@alignCast(ctx.allocBytes(16, 16))) };
}

pub fn classify(args: Args) usize {
    return if (args[0].?.unk3 == 128) 0 else 1;
}
