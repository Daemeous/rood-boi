//! zig_diff spec: vs_battle_setTextBox -> set_text_box.setTextBox (5BF94.c).
//! Inputs: a box index 0-7 with random flags (the 0x20 bit both ways), speed
//! (0, 0xFFFF, 1, 0xFFFE or any value), unk2E at the i16 edges (so unk2E + 1
//! is checked at 32767), and a string pointer that is either null or an arena
//! block. Null strings cover both sides of the speed test; non-null strings
//! cover the callee being run (flag clear) and skipped (flag set).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setTextBox));

pub const classes = [_][:0]const u8{ "null-idle", "null-busy", "str-drawn", "str-undrawn" };

pub const stubs = struct {
    /// The text-box drawer: it may update the first box's speed and its
    /// unk2C flag. The C never reads them back, but the state is compared.
    pub fn func_800AAD4C(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        _ = d;
        if (ctx.value(u1) == 1) v.g.textBoxes()[0].speed = ctx.value(u16);
        if (ctx.value(u1) == 1) v.g.textBoxes()[0].unk2C = ctx.value(i16);
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const boxId = ctx.range(c_int, 0, 7);
    const box = &v.g.textBoxes()[@intCast(boxId)];
    psx.setBits(&box.unk0, "unk0_24", ctx.int(u6));
    box.speed = ctx.pick(u16, &.{ 0, 0xFFFF, 1, 0xFFFE });
    box.unk2E = ctx.pick(i16, &.{ 0, -1, 32767, -32768 });
    box.unk4 = ctx.int(i16);
    box.prevChunksPrinted = ctx.int(u16);
    box.unk2C = ctx.int(i16);
    const str: [*c]u8 = if (ctx.chance(2)) null else ctx.allocBytes(16, 1);
    return .{ boxId, str };
}

pub fn classify(args: Args) usize {
    const box = &v.g.textBoxes()[@intCast(args[0])];
    if (args[1] == null) {
        return if (box.speed != 0xFFFF and box.speed != 0) 1 else 0;
    }
    return if ((psx.getBits(&box.unk0, "unk0_24") & 0x20) != 0) 2 else 3;
}
