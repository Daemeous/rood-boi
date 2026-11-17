//! Calls func_8006CDB8 with the low byte of a tagged field when its tag is 4 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D4D64 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Passes the low byte of a 32-bit field to func_8006CDB8 when its 16-bit tag is 4; always returns 1.
pub fn func_800D4D64(arg0: ?*v.vs_effects_EffectInstance) callconv(.c) c_int {
    const p = arg0.?;
    if (p.context.unk0.unk0 == 4) {
        c.func_8006CDB8(@as(c_int, psx.conv(u8, p.context.unk0.unk4.unk0)));
    }
    return 1;
}
