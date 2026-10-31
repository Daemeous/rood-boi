//! Links one four-word primitive into an ordering table and advances the scratchpad primitive cursor (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_battle_addTile (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Writes a tile primitive at the scratchpad cursor, links it after `before` and advances the cursor.
pub fn addTile(before: [*c]c_uint, rgb0: c_int, xy: c_int, wh: c_int) callconv(.c) void {
    const prim: [*]align(1) c_uint = @ptrCast(psx.get(v.g.vs_scratch().unk0).?);
    prim[0] = (before.* & 0xFFFFFF) | 0x03000000;
    prim[1] = psx.conv(c_uint, rgb0);
    prim[2] = psx.conv(c_uint, xy);
    prim[3] = psx.conv(c_uint, wh);
    before.* = (psx.addr(prim) << 8) >> 8;
    psx.put(&v.g.vs_scratch().unk0, @as(?*anyopaque, @ptrCast(prim + 4)));
}
