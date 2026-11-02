//! zig_diff spec: vs_battle_script_selectTextBox -> script_select_text_box.script_selectTextBox (4A0A8.c).
//! Inputs: a script buffer in the arena (script[1] random), any i16 arg1, and the
//! two globals D_800F4C2C (the "done" state 2 or not) and _textBoxSelectionPending
//! (zero or nonzero). The selection callee returns 0, a value whose low 16 bits are
//! zero (truncates to 0 as the C's short does), -1 and 1 (sign-bit and no-sign-bit
//! shorts), and random 32-bit values. It can also change the pending flag, and
//! setStateFlag can change D_800F4C2C.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_selectTextBox));

pub const classes = [_][:0]const u8{ "state-done", "pending-set", "pending-clear" };

pub const stubs = struct {
    pub fn vs_battle_selectTextBox(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        const sel = ctx.value(u8);
        if (ctx.value(u1) == 1) v.g._textBoxSelectionPending().* = ctx.value(u8);
        return switch (sel % 8) {
            0 => 0,
            1 => 0x10000,
            2 => -65536,
            3 => -1,
            4 => 1,
            else => ctx.value(c_int),
        };
    }

    pub fn vs_battle_setStateFlag(ctx: *zd.Ctx, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        if (ctx.value(u1) == 1) v.g.D_800F4C2C().* = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 1);
    v.g.D_800F4C2C().* = if (ctx.chance(4)) 2 else ctx.edge(u8, &.{ 0, 1, 3, 0xFF });
    if (ctx.chance(2)) {
        v.g._textBoxSelectionPending().* = 0;
    } else {
        v.g._textBoxSelectionPending().* = ctx.edge(u8, &.{ 1, 2, 0x80, 0xFF });
    }
    const script: [*c]u8 = buf;
    return .{ script, ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 }) };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800F4C2C().* == 2) return 0;
    if (v.g._textBoxSelectionPending().* != 0) return 1;
    return 2;
}
