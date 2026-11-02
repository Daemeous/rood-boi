//! zig_diff spec: vs_battle_script_showTextBox -> script_show_text_box.script_showTextBox (4A0A8.c).
//! Inputs: a 4-byte script in the arena (script[1] a text box 0-7, script[2] an
//! index into a 256-entry u16 table that D_800F4BB0 points at, script[3] zero or
//! not), a text box status that is free (0) or busy (1, 0x80, 0xFF), and a
//! setTextBox stub that returns -1, 0, 1, 256, INT_MIN and large positive values.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_showTextBox));

pub const classes = [_][:0]const u8{ "free-end", "free-continue", "busy" };

pub const stubs = struct {
    pub fn vs_battle_setTextBox(ctx: *zd.Ctx, box: anytype, text: anytype) c_int {
        _ = text;
        // The callee may change the status of the box it was given.
        if (ctx.value(u1) == 1) v.g.vs_battle_textBoxStatuses()[@intCast(box)] = ctx.value(u8);
        const r = ctx.value(u8) % 6;
        return switch (r) {
            0 => -1,
            1 => 0,
            2 => 1,
            3 => 256,
            4 => std.math.minInt(c_int),
            else => @as(c_int, @intCast(ctx.value(u29) + 2)),
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const script = ctx.allocBytes(4, 1);
    script[0] = ctx.int(u8);
    script[1] = ctx.range(u8, 0, 7);
    script[2] = ctx.int(u8);
    script[3] = ctx.edge(u8, &.{ 0, 0, 1, 0x80, 0xFF });

    // The table D_800F4BB0 points at: 256 entries the C indexes by script[2],
    // each a small index the C indexes again (all inside the 512-entry block).
    const table: [*]u16 = @ptrCast(@alignCast(ctx.allocBytes(512 * 2, 2)));
    for (0..256) |i| table[i] = ctx.range(u16, 0, 511);
    for (256..512) |i| table[i] = ctx.int(u16);
    v.g.D_800F4BB0().* = table;

    // The status of the box the script names: free half the time.
    const st = v.g.vs_battle_textBoxStatuses();
    st[script[1]] = if (ctx.chance(2)) 0 else ctx.edge(u8, &.{ 1, 0x80, 0xFF });

    return .{ script, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const script = args[0];
    if (v.g.vs_battle_textBoxStatuses()[script[1]] != 0) return 2;
    return if (script[3] == 0) 0 else 1;
}
