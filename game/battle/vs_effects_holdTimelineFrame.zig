//! Sets bit 0 of a script's unkD1C.unk30 block's flags (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_effects_holdTimelineFrame (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// unkD1C.unk30->unk1 |= 1.
pub fn vs_effects_holdTimelineFrame(script: ?*v.D_800F53B8_t) callconv(.c) void {
    psx.get(script.?.unkD1C.unk30).?.unk1 |= 1;
}
