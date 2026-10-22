//! Builds the curve vertices from the scratchpad basis and trig tables (src/BATTLE/BATTLE.PRG/573B8.c).
const psx = @import("common").psx;
const v = @import("views_573B8");

/// orig: func_800C0758 (src/BATTLE/BATTLE.PRG/573B8.c)
/// Writes segments + 1 curve vertices from `index` on, from the basis and trig tables in the scratchpad; returns the next index.
pub fn func_800C0758(phase: c_int, segments: c_int, index: c_int) callconv(.c) c_int {
    const trig: [*]i16 = @ptrFromInt(0x1F8003B0);
    const basis: [*][4]i16 = @ptrFromInt(0x1F800398);
    var idx = index;
    // The C takes (char*)D_800EB9B8 + 0x48 + index * 8 with the int product wrapped first.
    const base = v.g.D_800EB9B8().*;
    const scaled: c_int = index *% 8;
    var vaddr: usize = @intFromPtr(base) +% 0x48 +% @as(usize, @bitCast(@as(isize, scaled)));
    var i: c_int = 0;
    while (i <= segments) : ({
        idx +%= 1;
        i +%= 1;
        vaddr +%= 8;
    }) {
        const vertex: *align(1) v.menuCurveVertex = @ptrFromInt(vaddr);
        const idx1: usize = @intCast((i +% phase +% 8) & 31);
        const idx2: usize = @intCast((i +% phase) & 31);
        var component: usize = 0;
        while (component < 3) : (component += 1) {
            const b0: c_int = basis[0][component];
            const t1: c_int = @as(c_int, basis[1][component]) *% @as(c_int, trig[idx1]);
            const t2: c_int = @as(c_int, basis[2][component]) *% @as(c_int, trig[idx2]);
            const sum: c_int = b0 +% (t1 >> 12) +% (t2 >> 12);
            vertex.xyz[component] = psx.conv(i16, sum);
        }
        var flags: c_int = @intFromBool(i != 0);
        if (flags != 0 and segments != 32) {
            flags +%= @as(c_int, @intFromBool(segments -% i < 2));
        }
        vertex.flags = psx.conv(i16, flags);
    }
    return idx;
}
