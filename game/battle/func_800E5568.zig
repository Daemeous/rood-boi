//! Projects a shape's range along its angle: the range times the absolute cosine or sine of the angle (src/BATTLE/BATTLE.PRG/7CD68.c).
const psx = @import("common").psx;
const v = @import("views_7CD68");
const c = @import("c_7CD68");

/// orig: func_800E5568 (src/BATTLE/BATTLE.PRG/7CD68.c)
/// Returns a shape's range at its angle: the range scaled by |cos| or |sin| of the angle.
pub fn func_800E5568(arg0: ?*v.func_800E5568_t) callconv(.c) c_int {
    const p = arg0.?;
    const shape: u3 = psx.getBits(p, "shape");
    const mask: c_int = @as(c_int, 1) << @as(u5, shape);
    const angle: c_int = @as(c_int, psx.getBits(p, "angle")) << 7;

    var r: c_int = undefined;
    var sample: c_int = undefined;
    if ((mask & 0x16) != 0) {
        r = @as(c_int, p.rangeZ);
        sample = c.rcos(angle);
    } else if ((mask & 0x8) != 0) {
        r = @as(c_int, p.rangeY);
        sample = c.rcos(angle);
    } else {
        r = @as(c_int, p.rangeY);
        sample = c.rsin(angle);
    }

    const abs_sample: c_int = if (sample >= 0) sample else -%sample;
    return ((r << 5) *% abs_sample) >> 12;
}
