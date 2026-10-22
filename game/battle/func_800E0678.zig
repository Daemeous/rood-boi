//! Movement check: whether a step from the cached tile in one direction is open,
//! memoised per direction in the scratchpad (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: func_800E0678 (src/BATTLE/BATTLE.PRG/771A8.c)
/// Whether the tile one step from `state` in `direction` can be moved to, cached per direction.
pub fn func_800E0678(state: ?*v.directionalCacheState, direction: c_int) callconv(.c) c_int {
    const bits: *v.directionalCacheBits = @ptrFromInt(0x1F8003D0);
    // x86 masks a shift count to 5 bits; the object shifts by `direction` the same way.
    const shift: u5 = @truncate(@as(c_uint, @bitCast(direction)));
    const bit: c_int = @as(c_int, 1) << shift;

    if (((@as(c_int, bits.tested) >> shift) & 1) != 0) {
        return @intFromBool((@as(c_int, bits.valid) & bit) > 0);
    }

    const usable: bool = check: {
        if (c.func_800E02B4(@ptrCast(state), direction, 1) != 0) break :check false;

        const st = state.?;
        const entry = psx.at(@as([*]v.D_800F16EC_t, @ptrFromInt(0x1F8003EC)), direction);
        const x: c_int = @as(c_int, st.tile.p.x) +% @as(c_int, psx.getBits(entry, "dx"));
        const z: c_int = @as(c_int, st.tile.p.z) +% @as(c_int, psx.getBits(entry, "dz"));

        const bounds = v.g.D_800F58BC().*.?;
        if (x > @as(c_int, bounds.maxX) or x < @as(c_int, bounds.minX) or
            z > @as(c_int, bounds.maxZ) or z < @as(c_int, bounds.minZ))
        {
            break :check false;
        }

        // The tile table: a guest address in the scratchpad, rows of 32 u16. x and z are
        // at least minX and minZ (u8), so they are nonnegative here.
        const tile_base: u32 = @as(*const u32, @ptrFromInt(0x1F8003C0)).*;
        const tile_addr: usize = @as(usize, tile_base) + @as(usize, @intCast(z)) * 64 + @as(usize, @intCast(x)) * 2;
        const tile: u16 = @as(*align(1) const u16, @ptrFromInt(tile_addr)).*;
        if ((tile & 0x40) != 0) break :check false;

        if (@as(c_int, st.previous.p.x) == x and @as(c_int, st.previous.p.z) == z) break :check false;

        if (bounds.unk14 != 0 and c.func_800E42EC(x, z) +% @as(c_int, st.minimumHeight) < bounds.unk14) {
            return 0;
        }

        break :check true;
    };

    if (usable) {
        bits.tested = psx.conv(u8, @as(c_int, bits.tested) | bit);
        bits.valid = psx.conv(u8, @as(c_int, bits.valid) | bit);
        return 1;
    }

    bits.tested = psx.conv(u8, @as(c_int, bits.tested) | bit);
    bits.valid = psx.conv(u8, @as(c_int, bits.valid) & ~bit);
    return 0;
}
