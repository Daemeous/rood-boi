//! Shape visibility test: whether a shape's footprint lies inside the four clip edges of the view at `position` (pc/src/game/BATTLE/BATTLE.PRG/58578.c).
const psx = @import("common").psx;
const gte = @import("common").gte;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: vs_battle_isPointInTargetPrism (pc/src/game/BATTLE/BATTLE.PRG/58578.c)
/// Tests a shape's footprint against the view from `position`: nonzero when it is inside every clip edge.
pub fn vs_battle_isPointInTargetPrism(shape: ?*v.func_800C1564_t, position: [*c]c_ushort, angled: c_int) callconv(.c) c_int {
    var polygon: extern struct { corners: [5]c_int, clip: c_int } = undefined;
    var work: extern struct { local: v.SVECTOR, rotated: v.SVECTOR, origin: [4]i16 } = undefined;
    var matrix: v.MATRIX = undefined;
    const s = shape.?;

    work.origin[0] = s.unk8.vx;
    work.origin[1] = s.unk8.vy;
    work.origin[2] = s.unk8.vz;
    work.local.vx = psx.conv(i16, @as(c_int, s.unk4.values[3]) << 4);
    work.local.vy = psx.conv(i16, -%@as(c_int, s.unk2));
    work.local.vz = 0;
    _ = c.RotMatrix_gte(psx.cptr(c.SVECTOR, &work.local), psx.cptr(c.MATRIX, &matrix));

    const dst = [3]*i16{ &work.local.vx, &work.local.vy, &work.local.vz };
    var i: c_int = 0;
    while (i < 3) : (i += 1) {
        const k: usize = @intCast(i);
        const pos: c_int = psx.at(position, i).*;
        const origin: c_int = @as(u16, @bitCast(work.origin[k]));
        dst[k].* = psx.conv(i16, pos -% origin);
    }

    gte.applyMatrixSV(&work.rotated, &matrix, &work.local);
    if (@as(c_int, psx.conv(u16, -%@as(c_int, work.rotated.vy))) > (@as(c_int, s.unk4.values[1]) << 5)) {
        return 0;
    }

    const x: c_int = @as(c_int, s.unk4.values[0]) << 5;
    const z: c_int = @as(c_int, s.unk4.values[2]) << 5;
    var offsetX: c_int = 0;
    var offsetZ: c_int = 0;
    if (angled != 0) {
        offsetZ = z;
        offsetX = -%x;
    } else {
        offsetZ = 0;
        offsetX = offsetZ;
    }

    polygon.corners[0] = @as(c_int, psx.conv(u16, -%x)) | ((-%offsetZ) << 16);
    polygon.corners[1] = (-%offsetX) | ((-%z) << 16);
    polygon.corners[2] = x | (offsetZ << 16);
    polygon.corners[3] = @as(c_int, psx.conv(u16, offsetX)) | (z << 16);
    polygon.corners[4] = @as(c_int, psx.conv(u16, work.rotated.vx)) | (@as(c_int, work.rotated.vz) << 16);

    i = 0;
    while (i < 4) : (i += 1) {
        const cur: usize = @intCast(i);
        const next: usize = @intCast((i +% 1) & 3);
        // The ROOD_PC branch: PsyCross's gte_ldsxy3 loads through pointers.
        polygon.clip = gte.nclipOf(@bitCast(polygon.corners[4]), @bitCast(polygon.corners[cur]), @bitCast(polygon.corners[next]));
        if (polygon.clip < 0) {
            return 0;
        }
    }
    return 1;
}
