//! Object lookup for a D_800F45E0 slot (src/BATTLE/BATTLE.PRG/3A1A0.c).
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A3C00 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Finds the object for a D_800F45E0 slot's key and index; null for an empty slot.
pub fn func_800A3C00(slot: ?*v.D_800F45E0_t, index: c_uint) callconv(.c) ?*v.D_800F4538_t {
    // ROOD_PC: func_8009F530 passes D_800F45E0[0] when no object stacks on the
    // removed one; the slot can be empty (the PS1 reads low RAM here).
    const e = slot orelse return null;

    const raw: ?*anyopaque = c.func_800A3C34(
        @as(c_int, e.unk5C),
        @as(c_int, e.unk5E),
        @as(c_int, e.unk1E),
        @bitCast(index),
    );
    const aligned: ?*align(4) anyopaque = @alignCast(raw);
    return @ptrCast(aligned);
}
