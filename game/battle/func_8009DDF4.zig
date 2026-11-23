//! Fires a weapon-drawn actor's attack at its target and clears its idle state (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009DDF4 (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Runs the weapon-drawn attack of actor `arg0` against `arg1`, then clears its idle flags.
pub fn func_8009DDF4(arg0: c_int, arg1: ?*v.SVECTOR) callconv(.c) void {
    // ROOD_PC: an empty actor slot reads through NULL, reads PS1 address 0 (zeroes; its writes are dropped).
    const slot = psx.at(v.g.vs_actors_actorRecords(), arg0).*;
    var absent_actor: v.D_800F4538_t = undefined;
    const temp: *align(1) v.D_800F4538_t = if (slot) |p| p else blk: {
        absent_actor = std.mem.zeroes(v.D_800F4538_t);
        break :blk &absent_actor;
    };
    const hdr: *align(1) v.vs_actors_ModelInstance = &temp.unk0;

    if (psx.getBits(hdr, "weaponDrawn") != 0 and
        (hdr.slot == 0 or
            (psx.getBits(hdr, "unkA_5") == 0 and psx.getBits(hdr, "unkA_6") == 0 and
                psx.getBits(hdr, "unkA_0") != 2 and psx.getBits(hdr, "unkA_0") != 3)))
    {
        var var_s1: c_int = 4;

        if (temp.unk63C < 0x80) {
            hdr.facingDelta = psx.conv(i16, c.vs_actors_actorFacingDeltaToPoint(@ptrCast(temp), @ptrCast(arg1)));
            hdr.turnFrames = 4;
            var_s1 = 8;
        }

        const temp_v0: c_int = c.vs_actors_pitchToPoint(@ptrCast(temp), @ptrCast(arg1));
        var var_a1: c_int = 0x4F;

        if (temp_v0 >= -0xE4) {
            var_a1 = 0x50;
            if (temp_v0 >= 0xAC) {
                var_a1 = 0x51;
            }
        }

        _ = c.vs_actors_playActorAnimation(arg0, var_a1, 0, var_s1);
        c.vs_actors_headTrackingSpeed(@ptrCast(temp));
    }

    if (hdr.slot == 0) {
        if (psx.getBits(hdr, "unkA_0") != 3) {
            psx.setBits(hdr, "unkA_0", 0);
        }
        hdr.unk11 = 0;
    }
}
