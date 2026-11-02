//! zig_diff spec: func_800CBBCC -> func_800CBBCC.func_800CBBCC (5BF94.c).
//! Inputs: an arena image of 0..4 x 0..4 tiles (some empty, all full now
//! and then), its scale 4096 (sprites) or other (quads at the packet
//! cursor, an arena block), its position from far left to far right (the
//! culls, and the full-screen anchor), its ground colour 0, 0x8000 or other
//! and the dim flag D_800F51C8 0 or 1. vs_battle_setSprite's stub returns
//! one arena packet; the column and row tables sit in the scratchpad.
//! Some inputs put an edge on a cull's bound (the last column at 319, a
//! column at 319, a row at 239: the compiled `> 319` and `> 239`) or fill
//! the screen (4 x 8 tiles at 8192, the bottom row at 223, 224 or 240) for
//! the anchor's test.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CBBCC));

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

pub const classes = [_][:0]const u8{ "sprites", "quads", "ground" };

pub fn prepare(ctx: *zd.Ctx) Args {
    packet = @ptrCast(@alignCast(ctx.allocBytes(32, 16)));
    psx.put(&v.g.vs_scratch().unk0, @as(*anyopaque, @ptrCast(ctx.allocBytes(32 * 11 * 4 + 16, 16))));
    const image = ctx.alloc(v.gim_t);
    const dims: *[2]u8 = @ptrCast(&image.unk4);
    dims[0] = ctx.range(u8, 0, 4);
    dims[1] = ctx.range(u8, 0, 4);
    const tiles: [*]align(1) u16 = @ptrFromInt(@intFromPtr(image) + 0x200);
    const full = ctx.chance(3);
    for (0..32) |i| tiles[i] = if (!full and ctx.chance(3)) 0 else ctx.int(u16) | 1;
    image.unkC = ctx.pick(i16, &.{ 4096, 4096, 2048, 8192, 16383, 0 });
    image.unk8 = ctx.pick(i16, &.{ 0, -160, 160, -400, 400 });
    image.unkA = ctx.pick(i16, &.{ 0, -120, 120, -300, 300 });
    const columns: i32 = dims[0];
    const rows: i32 = dims[1];
    switch (ctx.range(u8, 0, 6)) {
        // At 4096 the edges land on the culls' bounds: the last column at 319, a column at 320.
        0 => {
            image.unkC = 4096;
            image.unk8 = @intCast(159 - columns * 32);
        },
        1 => {
            image.unkC = 4096;
            if (dims[0] == 0) dims[0] = 1;
            if (dims[1] == 0) dims[1] = 1;
            const k: i32 = ctx.range(i32, 0, @as(i32, dims[0]) - 1);
            image.unk8 = @intCast(159 - (2 * k - @as(i32, dims[0])) * 32); // column k at 319
            image.unkA = 0;
        },
        3 => {
            image.unkC = 4096;
            if (dims[0] == 0) dims[0] = 1;
            if (dims[1] == 0) dims[1] = 1;
            const k: i32 = ctx.range(i32, 0, @as(i32, dims[1])); // the last edge too
            image.unkA = @intCast(119 - (((2 * k - @as(i32, dims[1])) * 61440) >> 13)); // row k at 239
            image.unk8 = 0;
        },
        // The full-screen anchor's bounds: 4 x 8 full tiles at 8192, the top row at 0, the bottom at 224.
        2 => {
            image.unkC = 8192;
            dims[0] = 4;
            dims[1] = 8;
            for (0..32) |i| tiles[i] |= 1;
            image.unk8 = ctx.edge(i16, &.{ 0, 96, -96 });
            image.unkA = ctx.edge(i16, &.{ -16, -17, 0 }); // the bottom row at 224, 223, 240
        },
        else => {},
    }
    _ = rows;
    image.unkE = ctx.pick(i16, &.{ 0, 0, -0x8000, 0x7C1F });
    v.g.D_800F51C8().* = ctx.pick(u8, &.{ 0, 1 });
    return .{ image, ctx.int(c_int), @ptrCast(@alignCast(ctx.allocBytes(16, 16))) };
}

pub fn classify(args: Args) usize {
    const image = args[0].?;
    if (image.unkE != 0) return 2;
    return if ((image.unkC & 0x3FFF) == 4096) 0 else 1;
}
