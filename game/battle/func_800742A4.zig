//! Camera mode 4 and actor 0 turned toward a room point (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_800742A4 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets camera mode 4 and tick speed 2, turns actor 0 (func_8009E070 mode 4) toward the centre of the tile
/// vs_field_interactionFocus.unk0 names (x and z times 128 plus 64), then calls func_800CB50C.
pub fn func_800742A4() callconv(.c) void {
    // sp10[1] and sp10[3] are uninitialised in the original: func_8009E070 reads only vx and vz.
    var point = [4]i16{ 0, 0, 0, 0 };
    v.g._cameraMode().* = 4;
    v.g.vs_gametime_tickspeed().* = 2;
    point[0] = psx.conv(i16, (@as(c_int, psx.get(v.g.vs_field_interactionFocus().unk0).?.trigger.tileX) << 7) +% 0x40);
    point[2] = psx.conv(i16, (@as(c_int, psx.get(v.g.vs_field_interactionFocus().unk0).?.trigger.tileZ) << 7) +% 0x40);
    c.func_8009E070(0, &point, 4);
    c.func_800CB50C();
}
