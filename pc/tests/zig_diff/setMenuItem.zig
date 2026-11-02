//! zig_diff spec: vs_battle_setMenuItem -> set_menu_item.setMenuItem (5BF94.c).
//! Inputs: vs_battle_menuItems an arena array of 10 rows, the row 0..9;
//! x, y, w, backgroundWidth any (narrowed); the text 256 random-ish bytes
//! from the game's codes: printing ones, the spacing code 250 (copied with
//! the byte after it, even at the 31st place), non-printing codes 229..255
//! (dropped), the terminator 231 (often, at the end at the latest).
//! vs_battle_rMemzero is a recording stub.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setMenuItem));

pub const classes = [_][:0]const u8{ "terminated", "full" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const rows: *v.vs_battle_menuItem_t = @ptrCast(@alignCast(ctx.allocBytes(10 * @sizeOf(v.vs_battle_menuItem_t), 16)));
    v.g.vs_battle_menuItems().* = rows;
    const text = ctx.allocBytes(256, 16);
    const stop = ctx.pick(usize, &.{ 0, 5, 30, 31, 32, 255 }) % 256;
    for (0..256) |i| {
        text[i] = switch (ctx.range(u8, 0, 9)) {
            0 => 250,
            1 => ctx.range(u8, 229, 255),
            else => ctx.range(u8, 0, 228),
        };
        if (text[i] == 231) text[i] = 230;
        if (i == stop or i == 255) text[i] = 231;
    }
    return .{ ctx.range(c_int, 0, 9), ctx.int(c_int), ctx.int(c_int), ctx.int(c_int), ctx.int(c_int), text };
}

pub fn classify(args: Args) usize {
    var printed: usize = 0;
    var i: usize = 0;
    while (printed < 31) : (i += 1) {
        const ch = args[5][i];
        if (ch == 231) return 0;
        if (ch == 250) {
            printed += 2;
            i += 1;
        } else if (ch < 229) printed += 1;
    }
    return 1;
}
