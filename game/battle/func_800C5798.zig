//! Fills one battle item's limb-name entry from its source row (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800C5798 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Clears battle item `arg0` and sets its kind, timing and limb name from source row `arg0`.
pub fn func_800C5798(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) ?*v.func_800C56C0_t2 {
    const base = v.g.D_800EB9B8().*.?;
    const item = psx.at(&base.unk990, arg0);
    const src = psx.at(psx.get(base.unk40).?, arg0);

    c.vs_battle_rMemzero(item, @sizeOf(v.func_800C56C0_t2));
    item.unk0 = psx.conv(u8, arg1);
    item.unk6 = psx.conv(u8, @as(c_int, arg2) *% 11 +% 32);
    const temp_v1: i32 = src.unk4;
    item.unk8 = src.unk0;
    item.unkC = temp_v1;

    if (arg1 == 2) {
        item.unk4 = 320;
        // The limb name address plus one byte, as a guest address (a null entry gives 1).
        const limb = v.g.vs_battle_limbNames()[src.unk8];
        const addr: u32 = if (limb) |p| psx.addr(p) else 0;
        psx.put(&item.limbName, @as(?*u8, @ptrFromInt(@as(usize, addr +% 1))));
    } else {
        item.unk4 = -72;
        psx.put(&item.limbName, v.g.vs_battle_limbNames()[src.unk8]);
        item.unk7 = 1;
    }
    return item;
}
