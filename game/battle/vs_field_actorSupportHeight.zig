//! Clamps a height-like value to the nearest matching actor's offset (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: vs_field_actorSupportHeight (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c)
/// Returns the base value, sign-extended from 15 bits, lowered to the smallest matching actor's offset.
pub fn vs_field_actorSupportHeight(arg0: c_int, arg1: c_int) callconv(.c) c_int {
    var h: c_int = c.vs_field_interpolatedFloorHeight(arg0, arg1);
    // h <<= 17; h >>= 17: the shift is done on the bit pattern, then arithmetic.
    h = @bitCast(@as(c_uint, @bitCast(h)) << 17);
    h >>= 17;

    const actors = v.g.vs_field_gimmickInstances();
    var i: c_int = 0;
    while (i < 16) : (i +%= 1) {
        if (actors[@intCast(i)]) |actor| {
            if (@as(c_int, actor.unk1C) == arg0 and @as(c_int, actor.unk20) == arg1 and
                @as(c_int, actor.unk1E) -% 0x80 < h)
            {
                h = @as(c_int, actor.unk1E) -% 0x80;
            }
        }
    }
    return h;
}
