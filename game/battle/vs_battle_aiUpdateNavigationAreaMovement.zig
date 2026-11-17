//! Terrain movement: steers a movement state toward its destination tile (pc/src/game/BATTLE/BATTLE.PRG/771A8.c).
const psx = @import("common").psx;
const pad = @import("common").pad;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// Low nibble of the terrain entry at (z, x) in the tile table whose address the scratchpad slot 0x1F8003C0 holds.
fn terrainTile(z: u8, x: u8) c_int {
    const table: usize = @intFromPtr(pad.nav.cells.?);
    const off: u32 = @as(u32, z) *% 64 +% @as(u32, x) *% 2;
    const p: *align(1) const u16 = @ptrFromInt(table +% @as(usize, off));
    return @as(c_int, p.*) & 15;
}

/// orig: vs_battle_aiUpdateNavigationAreaMovement (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// Steps a terrain movement toward its destination, rerouting when the terrain class changes ahead.
pub fn vs_battle_aiUpdateNavigationAreaMovement(state: ?*v.vs_battle_aiState) callconv(.c) void {
    const st = state.?;
    const active: u8 = 1;
    st.flags4.b[0] = 0;
    st.flags4.b[1] = active;
    if (st.f1A != 0) {
        c.vs_battle_aiMoveWithHeightFilteredDirections(@ptrCast(st));
        return;
    }
    const current = terrainTile(st.tile.terrain.p.z, st.tile.terrain.p.x);
    const next = terrainTile(st.destination168.terrainMovementPoint.p.z, st.destination168.terrainMovementPoint.p.x);
    var at_move = false;
    if (current != next) {
        if (st.f29 != 0) {
            const mode = c.vs_battle_aiResolveNavigationAreaRoute(@ptrCast(st), current, next);
            if (mode == 6) {
                st.moveX = (@as(c_int, st.destination168.terrainMovementPoint.p.x) << 7) +% 64;
                st.moveZ = (@as(c_int, st.destination168.terrainMovementPoint.p.z) << 7) +% 64;
                if (terrainTile(st.destination168.terrainMovementPoint.p.z, st.destination168.terrainMovementPoint.p.x) == terrainTile(st.tile.terrain.p.z, st.tile.terrain.p.x)) {
                    at_move = true;
                }
            } else {
                c.func_800E4BB8(@ptrCast(st));
                if (mode == 5) {
                    c.func_800E4B70(@ptrCast(st), @as(c_int, st.heading) << 9);
                } else {
                    c.vs_battle_aiIssueActorCommand(@ptrCast(st), mode);
                }
                return;
            }
        }
        if (!at_move) {
            st.flags0.b[2] = active;
            pad.at(u8, 0x3D0).* = 0;
        }
    }
    // move:
    const heading = c.ratan2(
        @as(c_int, st.position.xyz[0]) -% st.moveX,
        @as(c_int, st.position.xyz[2]) -% st.moveZ,
    ) & 0xFFF;
    c.vs_battle_aiResolveDirectionalMovement(@ptrCast(st), heading, 0);
}
