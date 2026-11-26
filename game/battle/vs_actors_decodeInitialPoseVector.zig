//! Stores a 6-entry SVECTOR from six big-endian bytes and sets its pad flags (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_decodeInitialPoseVector (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Reads three big-endian 16-bit values from `arg1` (native_model's pc_nm_initial_vector), doubles them into `arg0`'s vector, and sets pad flags on it and on the entry 84 vectors on.
pub fn vs_actors_decodeInitialPoseVector(arg0: ?*v.SVECTOR, arg1: [*c]u8, arg2: c_int) callconv(.c) void {
    const sv = arg0.?;

    var turns: [3]c_short = undefined;
    c.pc_nm_initial_vector(arg1, &turns);
    sv.vx = psx.conv(i16, @as(c_int, turns[0]));
    sv.vy = psx.conv(i16, @as(c_int, turns[1]));
    sv.vz = psx.conv(i16, @as(c_int, turns[2]));

    sv.pad = 6;

    if (arg2 != 0xFF) {
        sv.pad = 0;
    }

    // arg0 += 84: 84 SVECTORs of 8 bytes on.
    psx.byteAt(v.SVECTOR, sv, 84 * 8).pad = 0;
}
