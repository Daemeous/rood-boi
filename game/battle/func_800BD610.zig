//! Camera setting defaults: a bit for each camera field still at its default (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: func_800BD610 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns a bit for each camera setting that is at its default value.
pub fn func_800BD610() callconv(.c) c_int {
    const d = v.g.D_800F4BA4().*.?;
    var temp: c_int = @intFromBool(d.lookAt.mode == 0xF);
    if (d.eye.mode == 0xF) temp |= 2;
    if (d.unk168.unkA == 0) temp |= 4;
    if (d.projectionDistance.unk9 == 0) temp |= 8;
    if (d.nearClip.unk9 == 0) temp |= 0x10;
    if (d.farClip.unk9 == 0) temp |= 0x20;
    if (d.unk25C.unk1E == 0) temp |= 0x40;
    if (@as(c_int, d.unk1A4.unkB) + @as(c_int, d.unk1B0.unkB) == 0) temp |= 0x80;
    return temp;
}
