//! Movement check: whether a step from the cached tile in one direction is open,
//! memoised per direction in the scratchpad (pc/src/game/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: vs_battle_aiCanFlyInDirection (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// Whether the tile one step from `state` in `direction` can be moved to, cached per direction.
pub fn vs_battle_aiCanFlyInDirection(state: ?*v.vs_battle_aiState, direction: c_int) callconv(.c) c_int {
    const bits = &pad.move.cache.bits;
    // x86 masks a shift count to 5 bits; the object shifts by `direction` the same way.
    const shift: u5 = @truncate(@as(c_uint, @bitCast(direction)));
    const bit: c_int = @as(c_int, 1) << shift;

    if (((@as(c_int, bits.flyTested) >> shift) & 1) != 0) {
        return @intFromBool((@as(c_int, bits.flyPassable) & bit) > 0);
    }

    const usable: bool = check: {
        if (c.vs_battle_aiFlightDirectionObstruction(@ptrCast(state), direction, 1) != 0) break :check false;

        const st = state.?;
        const entry = psx.at(pad.directions(v.D_800F16EC_t), direction);
        const x: c_int = @as(c_int, st.tile.cache.p.x) +% @as(c_int, psx.getBits(entry, "dx"));
        const z: c_int = @as(c_int, st.tile.cache.p.z) +% @as(c_int, psx.getBits(entry, "dz"));

        const bounds = v.g.vs_battle_aiRoomAiState().*.?;
        if (x > @as(c_int, bounds.maxX) or x < @as(c_int, bounds.minX) or
            z > @as(c_int, bounds.maxZ) or z < @as(c_int, bounds.minZ))
        {
            break :check false;
        }

        // The tile table: a guest address in the scratchpad, rows of 32 u16. x and z are
        // at least minX and minZ (u8), so they are nonnegative here.
        const tile_base: usize = @intFromPtr(pad.nav.cells.?);
        const tile_addr: usize = tile_base + @as(usize, @intCast(z)) * 64 + @as(usize, @intCast(x)) * 2;
        const tile: u16 = @as(*align(1) const u16, @ptrFromInt(tile_addr)).*;
        if ((tile & 0x40) != 0) break :check false;

        if (@as(c_int, st.previous.directionalCachePoint.p.x) == x and @as(c_int, st.previous.directionalCachePoint.p.z) == z) break :check false;

        if (bounds.unk14 != 0 and c.vs_field_groundHeightAtTile(x, z) +% @as(c_int, st.heightBE) < bounds.unk14) {
            return 0;
        }

        break :check true;
    };

    if (usable) {
        bits.flyTested = psx.conv(u8, @as(c_int, bits.flyTested) | bit);
        bits.flyPassable = psx.conv(u8, @as(c_int, bits.flyPassable) | bit);
        return 1;
    }

    bits.flyTested = psx.conv(u8, @as(c_int, bits.flyTested) | bit);
    bits.flyPassable = psx.conv(u8, @as(c_int, bits.flyPassable) & ~bit);
    return 0;
}
