//! Adds a battle action's flag byte to an actor's unk954 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_battle_addActionRecoveryDelay (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Adds an action's flag byte (or the weapon's 0xFF form) to an actor's unk954 when enabled and the action id is outside 0x16..0x35.
pub fn vs_battle_addActionRecoveryDelay(arg0: c_uint, arg1: ?*v.vs_battle_actor2, arg2: c_int) callconv(.c) void {
    if (arg2 != 0 and (arg0 -% 0x16) >= 0x20) {
        // The C does not check arg0 against the array length: psx.at keeps the unchecked address.
        const new_var = psx.at(v.g.actions(), arg0);
        const actor = arg1.?;
        if (new_var.flags_0 == 0xFF) {
            const sum: c_int = @as(c_int, actor.weapon.unk10B) + @as(c_int, psx.getBits(actor, "unk956_2"));
            actor.unk954 = psx.conv(u16, @as(c_int, actor.unk954) +% (sum << 8));
        } else {
            const new_var2: c_int = @as(c_int, new_var.flags_0) + @as(c_int, psx.getBits(actor, "unk956_2"));
            actor.unk954 = psx.conv(u16, @as(c_int, actor.unk954) +% (new_var2 << 8));
        }
    }
}
