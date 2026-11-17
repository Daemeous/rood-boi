//! Marks a terrain tile while the movement state's facing is computed, then
//! restores it (pc/src/game/BATTLE/BATTLE.PRG/771A8.c).
const psx = @import("common").psx;
const pad = @import("common").pad;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: vs_battle_aiMoveWithHeightFilteredDirections (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// Faces the actor of a terrain movement toward its target and sets its mode and angle.
pub fn vs_battle_aiMoveWithHeightFilteredDirections(state: ?*v.vs_battle_aiState) callconv(.c) void {
    const s = state.?;

    // TERRAIN_TILE_MASK base is the guest pointer stored at scratchpad 0x1F8003C4;
    // the row width is the byte at 0x1F8003DC. The index is never negative.
    const table: usize = @intFromPtr(pad.nav.bitmap.?);
    const width: c_int = pad.at(u8, 0x3DC).*;
    const index: c_int = @as(c_int, s.tile.terrain.p.z) *% width +% @as(c_int, s.tile.terrain.p.x);
    const tile: *align(1) u8 = @ptrFromInt(table +% @as(usize, @as(u32, @bitCast(index))));
    const old: c_int = tile.*;
    const actor = psx.get(s.actor);

    tile.* = c.vs_battle_aiFilterDirectionsByGroundHeight(psx.conv(u8, old), s.tile.terrain.raw, @as(c_int, s.position.xyz[1]));
    c.vs_battle_aiResolveDirectionalMovement(@ptrCast(s),
        c.ratan2(
            @as(c_int, s.position.xyz[0]) -% s.moveX,
            @as(c_int, s.position.xyz[2]) -% s.moveZ,
        ) & 0xFFF,
        0);
    tile.* = psx.conv(u8, old);

    const mode: c_int = pad.at(c_int, 0x3C8).*;
    // x86 masks a variable shift count to 5 bits; the C relies on that.
    const shift: u5 = @truncate(@as(u32, @bitCast(mode)));
    if (((@as(c_int, 0xA0) >> shift) & 1) != 0) {
        actor.?.unk9 = 9;
        if (mode == 5) {
            @as(*align(1) u16, @ptrCast(&actor.?.unkC)).* = psx.conv(u16, @as(c_int, s.heading) << 9);
        } else {
            @as(*align(1) u16, @ptrCast(&actor.?.unkC)).* = psx.conv(u16, s.heading);
        }
    }
}
