//! Battle record state query: 0 when the record is absent or closed, 1 when input is idle or held, else 2 (src/BATTLE/BATTLE.PRG/5BE50.c).
const v = @import("views_5BE50");

/// orig: func_800C4734 (src/BATTLE/BATTLE.PRG/5BE50.c)
/// Reports whether the battle record is open and whether its input is idle or held.
pub fn func_800C4734() callconv(.c) c_int {
    const rec = v.g.D_800EB9B8().* orelse return 0;
    if (rec.unk2A != 0) return 0;
    if (rec.unk3E == 0 or (v.g.vs_main_buttonsState().* & 0x80) != 0) return 1;
    return 2;
}
