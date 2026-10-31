//! Picks the menu slot for the active unit, redirected through the block 9 table when the unit has no predecessor (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D4DB0 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Chooses the slot the unit's menu opens on, redirects it through the block 9 table, and opens it.
pub fn func_800D4DB0(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const slot: u8 = c.func_800D5170(@ptrCast(arg0));
    var index: u8 = psx.at(&arg0.?.unk10, slot).*;

    if (psx.get(arg0.?.previous) == null and index < 4) {
        const block9 = psx.get(v.g.D_800F569C().*.?.block9Data).?;
        const id: c_int = block9.unk16[index];
        if (id != 0 and psx.get(v.g.D_800F569C().*.?.block3Data) != null and
            id < psx.get(v.g.D_800F569C().*.?.block3Data).?.count)
        {
            index = psx.conv(u8, id -% 1);
        }
    }

    c.func_800D55A4(@ptrCast(arg0), @as(c_int, index), 0);
    return 1;
}
