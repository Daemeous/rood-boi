//! Parses a pFile curve block: records it and the data start of each curve (src/BATTLE/BATTLE.PRG/6F014.c).
const psx = @import("common").psx;
const v = @import("views_6F014");

/// orig: _parsePfileCurveBlock (src/BATTLE/BATTLE.PRG/6F014.c)
/// Records the curve block, then the data start of each of its curves in the global curve table.
pub fn parsePfileCurveBlock(block: ?*v.pFileCurveBlock) callconv(.c) void {
    psx.put(&v.g.D_800F569C().*.?.curveBlock, block);
    const b = block.?;
    var p = psx.byteAt(u8, b, @as(isize, b.dataOffset));
    var i: c_int = 0;
    // count is re-read each pass, as the C does; curves has 32 entries and the C does not check count.
    while (i < @as(c_int, b.count)) : (i +%= 1) {
        psx.put(psx.at(&v.g.D_800F569C().*.?.curves, i), p);
        p = psx.byteAt(u8, p, @as(isize, psx.at(&b.curveSizes, i).*));
    }
}
