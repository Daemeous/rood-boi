//! Marks a terrain tile while the movement state's facing is computed, then
//! restores it (src/BATTLE/BATTLE.PRG/771A8.c).
const psx = @import("common").psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: func_800E1908 (src/BATTLE/BATTLE.PRG/771A8.c)
/// Faces the actor of a terrain movement toward its target and sets its mode and angle.
pub fn func_800E1908(state: ?*v.terrainMovementState) callconv(.c) void {
    const s = state.?;

    // TERRAIN_TILE_MASK base is the guest pointer stored at scratchpad 0x1F8003C4;
    // the row width is the byte at 0x1F8003DC. The index is never negative.
    const table: u32 = @as(*const u32, @ptrFromInt(0x1F8003C4)).*;
    const width: c_int = @as(*const u8, @ptrFromInt(0x1F8003DC)).*;
    const index: c_int = @as(c_int, s.tile.p.z) *% width +% @as(c_int, s.tile.p.x);
    const tile: *align(1) u8 = @ptrFromInt(@as(usize, table +% @as(u32, @bitCast(index))));
    const old: c_int = tile.*;
    const actor = psx.get(s.actor);

    tile.* = c.func_800E1850(psx.conv(u8, old), s.tile.raw, @as(c_int, s.position[1]));
    c.func_800E3600(@ptrCast(s),
        c.ratan2(
            @as(c_int, s.position[0]) -% s.targetX,
            @as(c_int, s.position[2]) -% s.targetZ,
        ) & 0xFFF,
        0);
    tile.* = psx.conv(u8, old);

    const mode: c_int = @as(*const c_int, @ptrFromInt(0x1F8003C8)).*;
    // x86 masks a variable shift count to 5 bits; the C relies on that.
    const shift: u5 = @truncate(@as(u32, @bitCast(mode)));
    if (((@as(c_int, 0xA0) >> shift) & 1) != 0) {
        actor.?.mode = 9;
        if (mode == 5) {
            actor.?.angle = psx.conv(u16, @as(c_int, s.angle) << 9);
        } else {
            actor.?.angle = psx.conv(u16, s.angle);
        }
    }
}
