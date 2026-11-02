//! Inserts a texture-page packet before an OT entry (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_battle_insertTpage (src/BATTLE/BATTLE.PRG/5BF94.c)
/// A 1-word packet holding `tpage` at the packet cursor, linked in before `before`'s successor; the cursor moves on.
pub fn insertTpage(tpage: c_int, before: [*c]c_uint) callconv(.c) void {
    const scratch = v.g.vs_scratch();
    const prim: [*]u32 = @ptrCast(@alignCast(psx.get(scratch.unk0).?));
    prim[0] = (1 << 24) | (before.* & 0xFFFFFF);
    prim[1] = @bitCast(tpage);
    before.* = (psx.addr(prim) << 8) >> 8;
    psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(prim + 2)));
}
