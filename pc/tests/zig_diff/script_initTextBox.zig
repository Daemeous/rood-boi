//! zig_diff spec: vs_battle_script_initTextBox -> script_init_text_box.script_initTextBox (4A0A8.c).
//! Inputs: an 11-byte opcode whose box nibbles are equal (a box centred on
//! itself) or not (centred on its parent, an arena box with edge x, y and
//! sizes), and edge position and size bytes (0, 0x7F, 0x80, 0xFF: the short
//! centres wrap). getTextBox returns the arena box for each id; getShort
//! gives the actor reference 0x1FFF, 0x2000, -1 or any (the < 0x2000 test).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_initTextBox));

pub const classes = [_][:0]const u8{ "own-centre", "parent-centre" };

var boxes: [16]*v.vs_battle_textBox = undefined;

pub fn prepare(ctx: *zd.Ctx) Args {
    for (&boxes) |*b| {
        b.* = ctx.alloc(v.vs_battle_textBox);
        b.*.x = ctx.pick(i16, &.{ 0, -1, 0x7FFF, -0x8000, 100 });
        b.*.y = ctx.pick(i16, &.{ 0, -1, 0x7FFF, -0x8000, 100 });
    }
    const s = ctx.allocBytes(11, 1);
    const id = ctx.int(u4);
    s[1] = if (ctx.chance(2)) (@as(u8, id) << 4) | id else ctx.int(u8);
    for (4..8) |i| s[i] = ctx.pick(u8, &.{ 0, 0x7F, 0x80, 0xFF });
    return .{ s, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const b = args[0][1];
    return if ((b & 0xF) == (b >> 4)) 0 else 1;
}

pub const stubs = struct {
    pub fn vs_battle_getTextBox(ctx: *zd.Ctx, id: c_int) ?*v.vs_battle_textBox {
        _ = ctx;
        return boxes[@as(usize, @intCast(id)) & 15];
    }
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return switch (ctx.value(u8) % 5) {
            0 => 0x1FFF,
            1 => 0x2000,
            2 => -1,
            3 => 0,
            else => ctx.value(c_short),
        };
    }
};
