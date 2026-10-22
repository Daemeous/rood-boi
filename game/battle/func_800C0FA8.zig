//! Builds a battle rotation block from a transform's values and its GTE rotation matrix (src/BATTLE/BATTLE.PRG/58578.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: func_800C0FA8 (src/BATTLE/BATTLE.PRG/58578.c)
/// Copies the transform's vector and scale terms into the rotation block, then builds its matrix with RotMatrix_gte.
pub fn func_800C0FA8(arg0: ?*v.func_800C1564_t, arg1: ?*v.func_800C0FA8_t2, arg2: ?*v.MATRIX) callconv(.c) void {
    const src = arg0.?;
    const dst = arg1.?;

    var i: c_int = 0;
    while (i < 3) : (i +%= 1) {
        const k: usize = @intCast(i);
        dst.unk10[k] = psx.at(&src.unk8.vx, i).*;
        dst.unk18[k] = psx.conv(i16, @divTrunc(@as(c_int, 0x8000), @as(c_int, src.unk4.values[k])));
    }

    dst.unk0.vx = psx.conv(i16, @as(c_int, src.unk4.values[3]) *% 0x10);
    dst.unk0.vy = psx.conv(i16, -%@as(c_int, src.unk2));
    dst.unk0.vz = 0;

    const mat: [*c]c.MATRIX = if (arg2) |m| psx.cptr(c.MATRIX, m) else null;
    _ = c.RotMatrix_gte(psx.cptr(c.SVECTOR, &dst.unk0), mat);
}
