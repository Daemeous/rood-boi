//! Looks up the room section for the position that vs_actors_actorPlacement reports
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_findFacingRoomObject (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the room section at the position from vs_actors_actorPlacement, or null when its height is outside the section's band.
pub fn vs_field_findFacingRoomObject(arg0: c_int) callconv(.c) ?*v._mpdRoomSectionA {
    // uninitialised in the original until vs_actors_actorPlacement and vs_field_tileInFrontOfActor fill them
    var sp10: v.func_8006EBF8_t = undefined;
    var sp20: [4]u8 = undefined;

    _ = c.vs_actors_actorPlacement(arg0, @ptrCast(&sp10));
    _ = c.vs_field_tileInFrontOfActor(@ptrCast(&sp10), &sp20[0]);

    // the section lookup runs before the height test, as in the original
    var temp_s1: ?*v._mpdRoomSectionA = @ptrCast(c.vs_field_interactiveDoorAtTile(
        @as(c_int, sp20[0]),
        @as(c_int, sp20[2]),
        @as(c_int, sp20[1]),
    ));

    const x: c_int = (@as(c_int, sp20[0]) << 7) | 0x40;
    const z: c_int = (@as(c_int, sp20[2]) << 7) | 0x40;
    const dist: c_short = c.vs_field_floorHeightAt(x, z);
    // sign-extend the low 15 bits of the 16-bit result
    const wide: c_int = (@as(c_int, dist) << 17) >> 17;
    const diff: c_int = wide -% @as(c_int, sp10.unk0.unk4.vy);
    const band: c_int = diff +% 0x3F;
    if (psx.conv(c_uint, band) >= 0x7F) {
        temp_s1 = null;
    }
    return temp_s1;
}
