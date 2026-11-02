//! An action effect: the stat change to risk, when nonzero (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80084F94 (src/BATTLE/BATTLE.PRG/146C.c)
/// Adds _calculateStatChange's amount to the entity's risk and marks riskEffect 2, when the amount is nonzero.
pub fn func_80084F94(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, hit: c_int, arg4: c_int) callconv(.c) c_int {
    const amount = c._calculateStatChange(@ptrCast(action), @ptrCast(source), @ptrCast(target), hit, arg4);
    if ((@as(c_int, amount) << 16) != 0) {
        const entity = target.?;
        entity.risk = psx.conv(i16, @as(c_int, entity.risk) +% amount);
        psx.setBits(entity, "riskEffect", 2);
    }
    return 0; // no return statement in the C (the result register's leftover)
}
