//! Builds a scaled rotation matrix in the scratchpad from three scale bytes and an angle (src/BATTLE/BATTLE.PRG/573B8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_573B8");
const c = @import("c_573B8");

/// orig: func_800C085C (src/BATTLE/BATTLE.PRG/573B8.c)
/// Builds the rotation for `angle`, applies the per-axis scale bytes to it, and returns the transposed matrix in the scratchpad.
pub fn func_800C085C(scale: [*c]u8, angle: c_int) callconv(.c) ?*v.MATRIX {
    const scratch: [*]i16 = @ptrFromInt(0x1F800350);
    const rotation: *v.MATRIX = @ptrFromInt(0x1F800330);

    scratch[0] = psx.conv(i16, -%(@as(c_int, scale[3]) << 4));
    scratch[1] = psx.conv(i16, angle);
    scratch[2] = 0;
    _ = c.RotMatrixYXZ_gte(
        psx.cptr(c.SVECTOR, @as(*v.SVECTOR, @ptrCast(scratch))),
        psx.cptr(c.MATRIX, rotation),
    );

    var axis: usize = 0;
    while (axis < 3) : (axis += 1) {
        const value = scale[axis];
        var component: usize = 0;
        while (component < 3) : (component += 1) {
            scratch[component] = if (axis == component)
                psx.conv(i16, @as(c_int, value) << 5)
            else
                0;
        }
        _ = c.ApplyMatrixSV(
            psx.cptr(c.MATRIX, rotation),
            psx.cptr(c.SVECTOR, @as(*v.SVECTOR, @ptrCast(scratch))),
            psx.cptr(c.SVECTOR, @as(*v.SVECTOR, @ptrCast(&scratch[8 + axis * 4]))),
        );
    }

    // Transpose in place: the output rows at scratch[8..19] are read and the
    // first nine entries written, in the C's order.
    axis = 0;
    while (axis < 3) : (axis += 1) {
        var component: usize = 0;
        while (component < 3) : (component += 1) {
            scratch[axis * 3 + component] = scratch[8 + component * 4 + axis];
        }
    }

    return @ptrFromInt(0x1F800350);
}
