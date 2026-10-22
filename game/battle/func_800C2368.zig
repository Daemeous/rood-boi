//! The circular menu's vertex rings and their scratchpad basis (src/BATTLE/BATTLE.PRG/58578.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: func_800C2368 (src/BATTLE/BATTLE.PRG/58578.c)
/// Writes four vertex rings of the circle from `index` into D_800EB9B8 and the basis in scratchpad, then returns the next index.
pub fn func_800C2368(step: c_int, radius: c_int, index: c_int) callconv(.c) c_int {
    const basis: [*]i16 = @ptrFromInt(0x1F800398);
    var idx = index;

    // (char*)D_800EB9B8 + (index * 8 + 0x48), with the original index.
    const table: ?*v.D_800EB9B8_t = v.g.D_800EB9B8().*;
    const table_base: u32 = if (table) |t| psx.addr(t) else 0;
    var vaddr: u32 = table_base +% psx.conv(u32, idx *% 8 +% 0x48);

    var base: c_int = 0;
    while (base < 0x1000) : (base +%= 0x400) {
        var z: c_int = 0;
        var y: c_int = 0;
        var x: c_int = 0;
        var phase: c_int = base -% step *% 4;
        var point: c_int = 0;
        while (point <= radius) : ({
            idx +%= 1;
            point +%= 1;
            phase +%= step;
            vaddr +%= 8;
        }) {
            const vertex: *align(1) v.menuCircleVertex = @ptrFromInt(@as(usize, vaddr));
            vertex.xyz[0] = psx.conv(i16, (x *% c.rcos(phase)) >> 12);
            vertex.xyz[1] = psx.conv(i16, y);
            vertex.xyz[2] = psx.conv(i16, (z *% c.rsin(phase)) >> 12);
            const i: c_int = @intFromBool(point != 0);
            vertex.flags = psx.conv(i16, i);
            x +%= 0x200;
            y -%= 0x200;
            z +%= 0x200;
        }
    }

    // radius <<= 9 in the original; the shifted value is used from here on.
    const r: c_int = radius << 9;

    var i: c_int = 8;
    while (i >= 0) : (i -%= 1) {
        basis[@intCast(i)] = 0;
    }
    basis[1] = psx.conv(i16, -%r);
    basis[3] = psx.conv(i16, r);
    basis[8] = psx.conv(i16, r);
    idx = c.func_800C2254(step *% 7, idx);
    if (r >= 5) {
        basis[1] = -0x800;
        basis[3] = 0x800;
        basis[8] = 0x800;
        idx = c.func_800C2254(step *% 4, idx);
    }
    return idx;
}
