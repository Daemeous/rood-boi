//! zig_diff spec: vs_battle_selectTextBox -> select_text_box.selectTextBox (5BF94.c).
//! Inputs: the selector idle (count 0: the boxes in a random mask are
//! ordered by their random heights, ties too; the state flags' choice 0..9,
//! 0 and past the count wrapping to the first) or running (count 1..8, the
//! choice below it, an order of 0..7) with the buttons none, a confirm
//! (bits 0..7), up (12), down (14) or both.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").selectTextBox));

pub const classes = [_][:0]const u8{ "start", "confirm", "move" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const selector = v.g.textBoxSelector();
    const boxes = v.g.textBoxes();
    for (0..8) |i| boxes[i].y = ctx.pick(i16, &.{ 0, 10, 10, 200, 0x7FFF, -1 });
    v.g.vs_main_stateFlags().unk11 = ctx.range(u8, 0, 9);
    if (ctx.random.boolean()) {
        selector.count = 0;
    } else {
        selector.count = ctx.range(u8, 1, 8);
        selector.selectedIndex = ctx.range(u8, 0, selector.count - 1);
        for (&selector.order) |*o| o.* = ctx.range(u8, 0, 7);
    }
    v.g.vs_main_buttonsPressed().all = ctx.pick(u32, &.{ 0, 1, 0x80, 1 << 12, 1 << 14, (1 << 12) | (1 << 14), 0x100 });
    return .{ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF, 0x5A })};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.textBoxSelector().count == 0) return 0;
    return if ((v.g.vs_main_buttonsPressed().all & 0xFF) != 0) 1 else 2;
}
