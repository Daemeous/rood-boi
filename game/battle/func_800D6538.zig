//! Sets bit 0 of a script's unkD1C.unk30 block's flags (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: func_800D6538 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// unkD1C.unk30->unk1 |= 1.
pub fn func_800D6538(script: ?*v.D_800F53B8_t) callconv(.c) void {
    psx.get(script.?.unkD1C.unk30).?.unk1 |= 1;
}
