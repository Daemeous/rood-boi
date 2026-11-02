//! The event script's start (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// The u16 at byte `off` of `p` (the C's `*(u_short*)(p + off)`).
fn half(p: [*c]u8, off: usize) u16 {
    return psx.byteAt(u16, p, @intCast(off)).*;
}

/// orig: func_800BF5F0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opens an event script block: D_800F4BB0 and the string tables D_800EB588[9..15] and
/// D_800F4BC0[0..7] from its header's offsets (a zero entry: none), the main thread at data + 16
/// (the three others stopped), and the script state, text boxes and flags 16..31 and 168 reset.
pub fn func_800BF5F0(data: [*c]u8) callconv(.c) void {
    // (u_short*)(data + offset): stored by its bits, as the C does (an odd offset is not 2-aligned)
    @as(*usize, @ptrCast(v.g.D_800F4BB0())).* = @intFromPtr(data + half(data, 2));
    var table = data + half(data, 4);
    var i: usize = 9;
    while (i < 16) : (i += 1) {
        const off = half(table, 2 * (i - 8));
        v.g.D_800EB588()[i] = if (off != 0) table + off else null;
    }
    table = data + half(data, 6);
    i = 0;
    while (i < 8) : (i += 1) {
        const off = half(table, 2 * i);
        v.g.D_800F4BC0()[i] = if (off != 0) table + off else null;
    }
    v.g.D_800F4C38()[0] = data + 16;
    v.g.D_800F4C38()[3] = null;
    v.g.D_800F4C38()[2] = null;
    v.g.D_800F4C38()[1] = null;
    v.g.D_800F4C2C().* = 0;
    v.g.D_800F4C68().* = 0;
    c.vs_battle_setStateFlag(168, 0);
    v.g.D_800F4C58()[3] = 0;
    v.g.D_800F4C58()[2] = 0;
    v.g.D_800F4C58()[1] = 0;
    v.g.D_800F4BB9().* = 0;
    v.g.D_800F4C4E().* = 0;
    v.g._textBoxSelectionPending().* = 0;
    v.g.D_800F4BE4().* = 0;
    v.g.D_800F4BF8().* = 0;
    v.g.D_800F4BE2().* = 0;
    v.g.D_800F4BB6().* = 0;
    v.g.D_800F4C58()[0] = 0;
    v.g.D_800F4C22().* = 0;
    i = 0;
    while (i < 8) : (i += 1) v.g.vs_battle_textBoxStatuses()[i] = 0;
    const pairs: [*]u8 = @ptrCast(v.g.D_800F4B28()); // (scriptBytePair*)D_800F4B28: {low, high}
    i = 0;
    while (i < 4) : (i += 1) {
        pairs[2 * i + 1] = 0;
        pairs[2 * i] = 0;
    }
    v.g.D_800F4B8B().* = 0;
    v.g.D_800F4B30()[2].unk3 = 0;
    v.g.D_800F4B30()[1].unk3 = 0;
    v.g.D_800F4B30()[0].unk3 = 0;
    v.g.D_800F4C69().* = psx.conv(u8, c.vs_main_getcurrentMusicSlot());
    v.g.D_800F4BBC().* = null;
    var flag: c_short = 16;
    while (flag < 32) : (flag += 1) c.vs_battle_setStateFlag(flag, 0);
}
