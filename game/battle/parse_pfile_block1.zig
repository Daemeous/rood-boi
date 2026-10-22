//! Parses block 1 of a PFILE: the data pointer and the table pointers (src/BATTLE/BATTLE.PRG/6F014.c).
const psx = @import("common").psx;
const v = @import("views_6F014");

/// orig: _parsePfileBlock1 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Records block 1's table count, its data and its tables as guest addresses inside the block.
pub fn parsePfileBlock1(block: ?*v.pFileBlock1) callconv(.c) void {
    const b = block.?;
    const d = v.g.D_800F569C().*.?;

    d.block1TableCount = b.tableCount;

    // (void*)block + dataOffset: the u16 offset zero-extends, the sum wraps in 32 bits.
    const dataAddr: u32 = psx.addr(b) +% @as(u32, b.dataOffset);
    const dataPtr: *align(1) v.pFileBlock1Data = @ptrFromInt(dataAddr);
    psx.put(&d.block1Data, dataPtr);

    var i: c_int = 0;
    while (i < d.block1TableCount) : (i +%= 1) {
        // (void*)block + tableOffsets[i]: the int offset sign-extends, the sum wraps in 32 bits.
        const off: i32 = psx.at(&b.tableOffsets, i).*;
        const tableAddr: u32 = psx.addr(b) +% psx.conv(u32, off);
        const tablePtr: *align(1) v.pFileBlock1Indices = @ptrFromInt(tableAddr);
        psx.put(psx.at(&d.block1Tables, i), tablePtr);
    }
}
