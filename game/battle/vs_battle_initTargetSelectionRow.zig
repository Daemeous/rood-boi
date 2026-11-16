//! Fills one battle item's limb-name entry from its source row (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_initTargetSelectionRow (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Clears battle item `arg0` and sets its kind, timing and limb name from source row `arg0`.
pub fn vs_battle_initTargetSelectionRow(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) ?*v.D_800EB9B8_unk990 {
    const base = v.g.vs_battle_targetSphere().*.?;
    const item = psx.at(&base.unk990, arg0);
    const src = psx.at(base.unk40.?, arg0);

    c.vs_battle_rMemzero(item, @sizeOf(v.D_800EB9B8_unk990));
    item.unk0 = psx.conv(u8, arg1);
    item.unk6 = psx.conv(u8, @as(c_int, arg2) *% 11 +% 32);
    const temp_v1: i32 = src.unk4;
    item.unk8.words[0] = src.unk0;
    item.unk8.words[1] = temp_v1;

    if (arg1 == 2) {
        item.unk4 = 320;
        // The limb name's address plus one byte (a null entry gives 1).
        const limb = v.g.vs_battle_limbNames()[src.unk8];
        const addr: usize = if (limb) |p| @intFromPtr(p) else 0;
        item.limbName = @ptrFromInt(addr +% 1);
    } else {
        item.unk4 = -72;
        item.limbName = v.g.vs_battle_limbNames()[src.unk8];
        item.unk7 = 1;
    }
    return item;
}
