//! Places an actor's position record at a signed offset from its position, with the offsets' signs from the input record (src/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");

/// orig: func_800A3054 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Sets the actor's secondary position to its position moved by 63 on each axis, negated where the input's component is negative.
pub fn func_800A3054(arg0: ?*v.D_800F4538_t, arg1: ?*v.func_800A3054_t) callconv(.c) void {
    const actor = arg0.?;
    const offsets = arg1.?;

    var var_a2: c_int = 0x3F;
    var var_a3: c_int = 0x3F;
    if (offsets.unk0 < 0) {
        var_a2 = -0x3F;
    }
    if (offsets.unk4 < 0) {
        var_a3 = -0x3F;
    }

    actor.unk181A = 1;
    // (u_short) cast of the position component: zero-extended before the int addition.
    actor.unk1814 = psx.conv(i16, @as(c_int, psx.conv(u16, actor.unk0.position.vx)) +% var_a2);
    actor.unk1818 = psx.conv(i16, @as(c_int, psx.conv(u16, actor.unk0.position.vz)) +% var_a3);
}
