//! Wander step: picks the next direction for a wandering actor from the open
//! neighbours of its goal tile (src/BATTLE/BATTLE.PRG/771A8.c).
const psx = @import("common").psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// Direction table entry `index` bytes past WANDER_DIRECTIONS (scratchpad 0x1F8003EC).
fn wanderDirection(index: c_int) *const v.D_800F16EC_t {
    return @ptrFromInt(0x1F8003EC + @as(usize, @intCast(index)));
}

/// WANDER_TILES[row][col]: a u16 table of 32 columns whose guest address is in
/// scratchpad 0x1F8003C0. No bounds check, as in the C.
fn wanderTile(row: c_int, col: c_int) c_int {
    const base: u32 = @as(*const u32, @ptrFromInt(0x1F8003C0)).*;
    const addr: usize = @as(usize, base) +% @as(usize, @intCast(row)) *% 64 +% @as(usize, @intCast(col)) *% 2;
    return @as(c_int, @as(*align(1) const u16, @ptrFromInt(addr)).*);
}

/// orig: func_800E1BB8 (src/BATTLE/BATTLE.PRG/771A8.c)
/// Picks the wander step's next direction from the open neighbours of the goal tile and advances the goal; returns 1 once more than maxCount steps are taken.
pub fn func_800E1BB8(state: ?*v.wanderState, tile: c_uint, maxCount: c_uint, _: c_int) callconv(.c) c_int {
    if (maxCount == 0) return -1;
    const st = state.?;
    if (st.count == 0) st.goal.raw = tile;
    var candidate = st.goal;
    var count: c_int = 0;
    var choices: [4]c_int = undefined;
    var i: c_int = 0;
    while (i < 4) : (i += 1) {
        if ((st.flags & 8) == 0) {
            if (@as(*const c_int, @ptrFromInt(0x1F8003F4)).* != 0) {
                const bytes: [*]const u8 = @ptrFromInt(@as(usize, @as(*const u32, @ptrFromInt(0x1F8003C4)).*));
                const gz = @as(c_int, psx.getBits(&st.goal.p, "z"));
                const dc = @as(c_int, @as(*const u8, @ptrFromInt(0x1F8003DC)).*);
                const gx = @as(c_int, psx.getBits(&st.goal.p, "x"));
                const idx: usize = @intCast(gz *% dc +% gx);
                const byte: c_int = @as(c_int, bytes[idx]);
                const y: c_int = (byte >> @as(u5, @intCast(i *% 2))) & 1;
                psx.setBits(&candidate.p, "y", psx.conv(u8, y));
            } else {
                psx.setBits(&candidate.p, "y", psx.conv(u8, c.func_800E4690(st.goal.raw, i *% 2)));
            }
            const offset = wanderDirection(i *% 2).*;
            psx.setBits(&candidate.p, "x", psx.conv(u8, @as(c_int, psx.getBits(&st.goal.p, "x")) +% @as(c_int, psx.getBits(&offset, "dx"))));
            psx.setBits(&candidate.p, "z", psx.conv(u8, @as(c_int, psx.getBits(&st.goal.p, "z")) +% @as(c_int, psx.getBits(&offset, "dz"))));
            if (psx.getBits(&candidate.p, "y") != 0) {
                if (c.func_800D96C8(@ptrCast(st), st.goal.raw, candidate.raw, i *% 2) == 0) continue;
                psx.setBits(&candidate.p, "y", 0);
            }
            if (st.flag2 == 0 and
                (wanderTile(@as(c_int, psx.getBits(&candidate.p, "z")), @as(c_int, psx.getBits(&candidate.p, "x"))) & 15) !=
                    (wanderTile(@as(c_int, psx.getBits(&st.goal.p, "z")), @as(c_int, psx.getBits(&st.goal.p, "x"))) & 15))
                continue;
        }
        if ((wanderTile(@as(c_int, psx.getBits(&candidate.p, "z")), @as(c_int, psx.getBits(&candidate.p, "x"))) & 32) == 0) {
            choices[@intCast(count)] = i *% 2;
            count +%= 1;
        }
    }
    if (count == 0) return -1;
    var chosen: c_uint = psx.conv(c_uint, c.func_800E45D4(4096)) % psx.conv(c_uint, count);
    if (st.angle > 0 and ((@as(c_int, st.angle) +% 4) & 7) == choices[@intCast(chosen)]) {
        chosen = psx.conv(c_uint, c.func_800E45D4(4096)) % psx.conv(c_uint, count);
    }
    const dirX = wanderDirection(choices[@intCast(chosen)]);
    psx.setBits(&st.goal.p, "x", psx.conv(u8, @as(c_int, psx.getBits(&st.goal.p, "x")) +% @as(c_int, psx.getBits(dirX, "dx"))));
    const dirZ = wanderDirection(choices[@intCast(chosen)]);
    psx.setBits(&st.goal.p, "z", psx.conv(u8, @as(c_int, psx.getBits(&st.goal.p, "z")) +% @as(c_int, psx.getBits(dirZ, "dz"))));
    st.angle = psx.conv(i16, choices[@intCast(chosen)]);
    st.count +%= 1;
    return @as(c_int, @intFromBool(maxCount < @as(c_uint, st.count)));
}
