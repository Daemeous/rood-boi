//! Turn angle from an actor's facing to a point, in 12-bit units
//! (src/BATTLE/BATTLE.PRG/30DB0.c).
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009E180 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Signed turn (-0x800..0x800) from the actor's facing to `arg1`; 0 when `arg1` is null.
pub fn func_8009E180(arg0: ?*v.D_800F4538_t, arg1: ?*v.SVECTOR) callconv(.c) c_int {
    // The null check comes before any use of arg0, as in the C.
    const target = arg1 orelse return 0;
    const self = arg0.?;

    const dx: c_int = @as(c_int, target.vx) -% @as(c_int, self.unk0.position.vx);
    const dz: c_int = @as(c_int, target.vz) -% @as(c_int, self.unk0.position.vz);
    const facing: c_int = @as(c_int, self.unk0.facing);

    var dir: c_int = undefined;
    if (dx == 0 and dz == 0) {
        dir = facing;
    } else {
        dir = (0xC00 -% c.ratan2(dz, dx)) & 0xFFF;
    }

    dir -%= facing;

    var result: c_int = undefined;
    if (dir >= 0x801) {
        result = dir -% 0x1000;
    } else {
        result = dir;
        if (dir < -0x800) {
            result = dir +% 0x1000;
        }
    }
    return result;
}
