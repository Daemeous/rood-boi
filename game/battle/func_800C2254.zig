//! Builds the 33-point menu circle of vertices from the scratchpad basis (src/BATTLE/BATTLE.PRG/58578.c).
const psx = @import("common").psx;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: func_800C2254 (src/BATTLE/BATTLE.PRG/58578.c)
/// Writes 33 circle vertices from the scratchpad basis at `angle` into the table from `index`; returns the index after them.
pub fn func_800C2254(angle: c_int, index: c_int) callconv(.c) c_int {
    const basis: *[3][3]i16 = @ptrFromInt(0x1F800398);
    const base: [*]u8 = @ptrCast(v.g.D_800EB9B8().*.?);
    var idx = index;
    var point: c_int = 0;
    var phase: c_int = angle;
    // Byte offset of the current vertex: index * 8 + 0x48, advanced by 8 per point.
    var offset: isize = @as(isize, idx *% 8 +% 0x48);
    while (point < 33) : ({
        idx +%= 1;
        phase +%= 128;
        point +%= 1;
        offset += 8;
    }) {
        var component: c_int = 0;
        while (component < 3) : (component += 1) {
            const cosine: c_int = c.rcos(phase);
            const sine: c_int = c.rsin(phase);
            const comp: usize = @intCast(component);
            const value: c_int = @as(c_int, basis[0][comp]) +%
                ((@as(c_int, basis[1][comp]) *% cosine) >> 12) +%
                ((@as(c_int, basis[2][comp]) *% sine) >> 12);
            const vertex = psx.byteAt(v.menuCircleVertex, base, offset);
            vertex.xyz[comp] = psx.conv(i16, value);
        }
        const flag: c_int = @intFromBool(point != 0);
        const vertex = psx.byteAt(v.menuCircleVertex, base, offset);
        vertex.flags = psx.conv(i16, flag);
    }
    return idx;
}
