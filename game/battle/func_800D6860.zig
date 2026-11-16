//! An effect-script opcode: vs_battle_armDefenseAbilityTimingWindow on a kind-4 target (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D6860 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// If the script's unk24 target is of kind 4, vs_battle_armDefenseAbilityTimingWindow on its id's low byte.
pub fn func_800D6860(script: ?*v.D_800F53B8_t) callconv(.c) void {
    const s = script.?;
    if (s.context.unk24.unk0 == 4) {
        c.vs_battle_armDefenseAbilityTimingWindow(@as(c_int, psx.conv(u8, s.context.unk24.unk4.unk0)));
    }
}
