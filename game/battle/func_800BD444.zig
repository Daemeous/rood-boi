//! Sets angle track 0xE2 (eye) or 0xE3 (look-at) from a script command (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BD444 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets an angle track from a script command: an immediate angle, or a turn the short way to a new one.
pub fn func_800BD444(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // The C has no default arm and leaves var_s0 uninitialised for any other opcode (the object uses a stale
    // register); not reachable through the opcode table. The 0xE3 arm here.
    const track: *v.func_800BD57C_t = if (arg0[0] == 0xE2)
        &v.g.D_800F4BA4().*.?.unk168
    else
        &v.g.D_800F4BA4().*.?.unk174;

    track.unkA = psx.conv(i16, arg0[4]);

    if (track.unkA == 0) {
        track.unk2 = c.vs_battle_getShort(arg0 + 1);
        track.unk0 = 1;
    } else {
        track.unk4 = track.unk2;
        const angle: c_int = c.vs_battle_getShort(arg0 + 1);
        track.unk6 = psx.conv(i16, angle -% @as(c_int, track.unk2));

        if (@as(c_int, track.unk6) < -(4096 / 2)) {
            track.unk6 = psx.conv(i16, @as(c_int, track.unk6) +% 4096);
        } else if (@as(c_int, track.unk6) > (4096 / 2)) {
            track.unk6 = psx.conv(i16, @as(c_int, track.unk6) -% 4096);
        }

        switch (@as(c_int, arg0[3]) >> 4) {
            0 => {
                if (@as(c_int, track.unk6) > 0) {
                    track.unk6 = psx.conv(i16, @as(c_int, track.unk6) -% 4096);
                }
            },
            2 => {
                if (@as(c_int, track.unk6) < 0) {
                    track.unk6 = psx.conv(i16, @as(c_int, track.unk6) +% 4096);
                }
            },
            else => {},
        }

        track.unk1 = arg0[3] & 0xF;
        track.unk8 = 0;
    }
    return 0;
}
