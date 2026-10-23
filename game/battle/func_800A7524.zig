//! Wall contact response on an actor's motion: the scratchpad's collision contacts nudge its velocity (src/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");

/// orig: func_800A7524 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Turns the scratchpad's collision contacts into a push on an actor's motion, stopping blocked velocity, when the actor's flag 0x1000 is set.
pub fn func_800A7524(actor: ?*v.D_800F4538_t, motion: ?*v.SVECTOR) callconv(.c) void {
    const a = actor.?;
    if (psx.getBits(a, "unk5AC_12") != 0) {
        const scratch: *v.collisionResponseScratch = @ptrFromInt(0x1F8003BC);
        const contacts = &scratch.contacts;
        var x: c_int = 0;
        var z: c_int = @as(c_int, @intFromBool(contacts[0] != 0)) *% 16;
        if (contacts[1] != 0) {
            x = 8;
            z +%= 8;
        }
        if (contacts[2] != 0) x +%= 16;
        if (contacts[3] != 0) {
            x +%= 8;
            z -%= 8;
        }
        if (contacts[4] != 0) z -%= 16;
        if (contacts[5] != 0) {
            x -%= 8;
            z -%= 8;
        }
        if (contacts[6] != 0) x -%= 16;
        if (contacts[7] != 0) {
            x -%= 8;
            z +%= 8;
        }
        if (x > 16) x = 16;
        if (x < -16) x = -16;
        if (z > 16) z = 16;
        if (z < -16) z = -16;

        const m = motion.?;
        if (x != 0) {
            if (x < 0) {
                if (m.vx > 0) m.vx = 0;
            } else if (m.vx < 0) {
                m.vx = 0;
            }
        }
        if (z != 0) {
            if (z < 0) {
                if (m.vz > 0) m.vz = 0;
            } else if (m.vz < 0) {
                m.vz = 0;
            }
        }
        m.vx = psx.conv(i16, @as(c_int, m.vx) +% x);
        m.vz = psx.conv(i16, @as(c_int, m.vz) +% z);
        a.unk6EE = 0;
        a.unk6EF = 0;
    }
}
