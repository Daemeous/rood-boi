//! Finds the battle actor standing on a tile at or above a height, or the actor
//! whose scene position matches it (src/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");

/// orig: func_800A3C34 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Returns the first actor from slot `mode` (slot 0 when mode < 2) that occupies the given tile and sits at or above `y_floor - 192`.
pub fn func_800A3C34(tile_x: c_int, tile_z: c_int, y_floor: c_int, mode: c_int) callconv(.c) ?*v.D_800F4538_t {
    const y_min = y_floor -% 192;
    var i: c_int = if (mode < 2) 0 else mode;
    const table = v.g.D_800F4538();

    while (i < 17) : (i +%= 1) {
        // i is in 0..16 here: the loop test bounds it and the start is never negative.
        const actor = table[@intCast(i)] orelse continue;
        if (psx.getBits(&actor.unk0, "skip") != 0 or actor.unk6E6 == 0x7F) continue;

        if (mode == 1 and ((psx.getBits(&actor.unk0, "unkB_4") != 0 and psx.getBits(&actor.unk0, "unkB_4") < 3) or
            psx.getBits(&actor.unk0, "unkA_0") != 0 or
            psx.getBits(&actor.unk0, "unkB_0") != 0))
        {
            const pos = &actor.unk5EC;
            if ((@as(c_int, pos.vx) & 0xFF) == tile_x and tile_z == (@as(c_int, pos.vy) & 0xFF)) return actor;
            continue;
        }

        // The word at 0x5AC (the C's `(int*)&unk5B4 - 2`): its top nibble is unk5AC_28.
        if (i == 0 and (psx.byteAt(c_uint, actor, 0x5AC).* & 0xF000_0000) != 0) {
            const temp = v.g.D_800F45E0()[@as(usize, psx.getBits(actor, "unk5AC_28"))].?;
            if (tile_x == @as(c_int, temp.unk5C) and tile_z == @as(c_int, temp.unk5E)) return actor;
            continue;
        }

        if (psx.getBits(&actor.unk0, "unkA_3") != 2 and
            @as(c_int, actor.unk0.currentTileX) == tile_x and
            @as(c_int, actor.unk0.currentTileZ) == tile_z and
            psx.getBits(&actor.unk0, "unkA_5") == 0 and
            @as(c_int, actor.unk0.position.vy) >= y_min)
        {
            return actor;
        }
    }
    return null;
}
