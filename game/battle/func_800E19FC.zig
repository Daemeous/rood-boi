//! Terrain movement: steers a movement state toward its destination tile (src/BATTLE/BATTLE.PRG/771A8.c).
const psx = @import("common").psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// Low nibble of the terrain entry at (z, x) in the tile table whose address the scratchpad slot 0x1F8003C0 holds.
fn terrainTile(z: u8, x: u8) c_int {
    const table: u32 = @as(*const u32, @ptrFromInt(0x1F8003C0)).*;
    const off: u32 = @as(u32, z) *% 64 +% @as(u32, x) *% 2;
    const p: *align(1) const u16 = @ptrFromInt(@as(usize, table +% off));
    return @as(c_int, p.*) & 15;
}

/// orig: func_800E19FC (src/BATTLE/BATTLE.PRG/771A8.c)
/// Steps a terrain movement toward its destination, rerouting when the terrain class changes ahead.
pub fn func_800E19FC(state: ?*v.terrainMovementState) callconv(.c) void {
    const st = state.?;
    const active: u8 = 1;
    st.f4 = 0;
    st.f5 = active;
    if (st.f1A != 0) {
        c.func_800E1908(@ptrCast(st));
        return;
    }
    const current = terrainTile(st.tile.p.z, st.tile.p.x);
    const next = terrainTile(st.destination.p.z, st.destination.p.x);
    var at_move = false;
    if (current != next) {
        if (st.f29 != 0) {
            const mode = c.func_800DFBCC(@ptrCast(st), current, next);
            if (mode == 6) {
                st.targetX = (@as(c_int, st.destination.p.x) << 7) +% 64;
                st.targetZ = (@as(c_int, st.destination.p.z) << 7) +% 64;
                if (terrainTile(st.destination.p.z, st.destination.p.x) == terrainTile(st.tile.p.z, st.tile.p.x)) {
                    at_move = true;
                }
            } else {
                c.func_800E4BB8(@ptrCast(st));
                if (mode == 5) {
                    c.func_800E4B70(@ptrCast(st), @as(c_int, st.angle) << 9);
                } else {
                    c.func_800DEEFC(@ptrCast(st), mode);
                }
                return;
            }
        }
        if (!at_move) {
            st.f2 = active;
            @as(*u8, @ptrFromInt(0x1F8003D0)).* = 0;
        }
    }
    // move:
    const heading = c.ratan2(
        @as(c_int, st.position[0]) -% st.targetX,
        @as(c_int, st.position[2]) -% st.targetZ,
    ) & 0xFFF;
    c.func_800E3600(@ptrCast(st), heading, 0);
}
