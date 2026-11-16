//! Stores the clamped value, the first slot and the flags of the battle record vs_battle_targetSphere (pc/src/game/BATTLE/BATTLE.PRG/58578.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: vs_battle_setSphereTargets (pc/src/game/BATTLE/BATTLE.PRG/58578.c)
/// Sets the record's count (clamped to 24), its first slot and its flag byte, then calls vs_battle_updateTargetSelectionMenu(0).
pub fn vs_battle_setSphereTargets(arg0: c_int, arg1: ?*anyopaque, arg2: c_int) callconv(.c) void {
    var a0: c_int = arg0;
    const d = v.g.vs_battle_targetSphere().* orelse return;

    if (a0 >= 0x19) {
        a0 = 0x18;
    }
    d.unk2C = psx.conv(u8, a0);
    d.unk40 = @ptrCast(@alignCast(arg1));
    if (@as(c_int, d.unk2D) >= a0) {
        var v0: c_int = 0;
        if (a0 == 0) {
            v0 = 0xFF;
        }
        d.unk2D = psx.conv(u8, v0);
    }
    d.unk31 = psx.conv(u8, arg2);
    _ = c.vs_battle_updateTargetSelectionMenu(null);
}
