//! Sets angle track 0xE2 (eye) or 0xE3 (look-at) from a script command (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BD444 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets an angle track from a script command: an immediate angle, or a turn the short way to a new one.
pub fn func_800BD444(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // The C has no default arm and leaves var_s0 uninitialised for any other opcode (the object uses a stale
    // register); not reachable through the opcode table. The 0xE3 arm here.
    const track: *v.vs_event_AngleTween = if (arg0[0] == 0xE2)
        &v.g.vs_event_camera().*.?.unk168
    else
        &v.g.vs_event_camera().*.?.unk174;

    track.durationFrames = psx.conv(i16, arg0[4]);

    if (track.durationFrames == 0) {
        track.angle = c.vs_battle_getShort(arg0 + 1);
        track.hasChanged = 1;
    } else {
        track.startAngle = track.angle;
        const angle: c_int = c.vs_battle_getShort(arg0 + 1);
        track.deltaAngle = psx.conv(i16, angle -% @as(c_int, track.angle));

        if (@as(c_int, track.deltaAngle) < -(4096 / 2)) {
            track.deltaAngle = psx.conv(i16, @as(c_int, track.deltaAngle) +% 4096);
        } else if (@as(c_int, track.deltaAngle) > (4096 / 2)) {
            track.deltaAngle = psx.conv(i16, @as(c_int, track.deltaAngle) -% 4096);
        }

        switch (@as(c_int, arg0[3]) >> 4) {
            0 => {
                if (@as(c_int, track.deltaAngle) > 0) {
                    track.deltaAngle = psx.conv(i16, @as(c_int, track.deltaAngle) -% 4096);
                }
            },
            2 => {
                if (@as(c_int, track.deltaAngle) < 0) {
                    track.deltaAngle = psx.conv(i16, @as(c_int, track.deltaAngle) +% 4096);
                }
            },
            else => {},
        }

        track.easingMode = arg0[3] & 0xF;
        track.elapsedFrames = 0;
    }
    return 0;
}
