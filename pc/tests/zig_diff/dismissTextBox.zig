//! zig_diff spec: vs_battle_dismissTextBox -> dismiss_text_box.dismissTextBox (5BF94.c).
//! Inputs: a text box index 0-7 (the table's eight entries), its unk0_24 flags
//! with and without the 0x20 bit (the call is skipped when set), and its state
//! at 0, 1, -1 and the i16 extremes. The stub for func_800AAD4C sometimes
//! rewrites box 0's state and flags, so the C must re-read them after the call,
//! and returns random values the C ignores.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").dismissTextBox));

pub const classes = [_][:0]const u8{ "flag-0x20-set", "idle", "positive-state", "negative-state" };

pub const stubs = struct {
    pub fn func_800AAD4C(ctx: *zd.Ctx, a: c_int, b: c_int, c: c_int, d: c_int) c_int {
        _ = a;
        _ = b;
        _ = c;
        _ = d;
        // The callee may change the text box it was not given a pointer to.
        if (ctx.value(u1) == 1) v.g.textBoxes()[0].state = ctx.value(i16);
        if (ctx.value(u1) == 1) psx.setBits(&v.g.textBoxes()[0].unk0, "unk0_24", ctx.value(u6));
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.range(c_int, 0, 7);
    const box = &v.g.textBoxes()[@intCast(id)];
    psx.setBits(&box.unk0, "unk0_24", ctx.int(u6));
    box.state = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    return .{id};
}

pub fn classify(args: Args) usize {
    const box = &v.g.textBoxes()[@intCast(args[0])];
    if ((psx.getBits(&box.unk0, "unk0_24") & 0x20) != 0) return 0;
    if (box.state == 0) return 1;
    if (box.state > 0) return 2;
    return 3;
}
