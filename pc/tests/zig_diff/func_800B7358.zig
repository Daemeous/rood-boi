//! zig_diff spec: func_800B7358 -> func_800B7358.func_800B7358 (4A0A8.c).
//! Inputs: a 16-byte buffer whose byte 1 is an edge value (every bank 0-7 and
//! the high bit set, so a signed-shift slip shows), a random arg1, and a text
//! box in the arena that the stub returns. The C dereferences the box without
//! a null test, so the stub always returns a valid one. The state comparison
//! covers the store to the text box's unk22 byte.
const std = @import("std");
const zd = @import("zdiff");
const c = @import("c_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B7358));

pub const classes = [_][:0]const u8{ "bank0", "bank1", "bank2", "bank3", "bank4", "bank5", "bank6", "bank7" };

var textbox: [*]u8 = undefined;

pub const stubs = struct {
    pub fn vs_battle_getTextBox(ctx: *zd.Ctx, arg0: anytype) ?*c.vs_battle_textBox {
        _ = ctx;
        _ = arg0;
        return @ptrCast(@alignCast(textbox));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*]u8 = ctx.allocBytes(16, 16);
    buf[0] = ctx.int(u8);
    buf[1] = ctx.pick(u8, &.{ 0x00, 0x1F, 0x20, 0x3F, 0x40, 0x80, 0x9F, 0xE0, 0xFF });
    textbox = ctx.allocBytes(512, 16);
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return @as(usize, args[0][1] >> 5);
}
