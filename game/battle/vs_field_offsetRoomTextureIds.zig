//! Adds a delta to the short fields of each table block's records
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;

/// orig: vs_field_offsetRoomTextureIds (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Adds `arg0` to the short at +0x1E of every 0x20-byte record, then every 0x28-byte record, of each table block.
pub fn vs_field_offsetRoomTextureIds(arg0: c_int, arg1: [*c]c_int) callconv(.c) void {
    const base: usize = @intFromPtr(arg1);
    const blocks: c_int = readI32(base);
    var block: usize = base +% 4;
    var i: c_int = 0;
    while (i < blocks) : (i +%= 1) {
        // var_a1[7] >> 2 is an int* element offset: 4 bytes per element.
        const offset: c_int = readI32(block +% 28) >> 2;
        var rec: usize = base +% @as(usize, @bitCast(@as(isize, offset) *% 4));
        const n0: c_int = readI32(rec);
        rec +%= 4;
        const n1: c_int = readI32(rec);
        rec +%= 4;

        var j: c_int = 0;
        while (j < n0) : ({
            j +%= 1;
            rec +%= 0x20;
        }) {
            addShort(rec +% 0x1E, arg0);
        }

        j = 0;
        while (j < n1) : ({
            j +%= 1;
            rec +%= 0x28;
        }) {
            addShort(rec +% 0x1E, arg0);
        }

        block +%= 0x40;
    }
}

/// Reads a guest int at a guest address (byte-aligned access, as x86 does).
fn readI32(addr: usize) c_int {
    const p: *align(1) const c_int = @ptrFromInt(addr);
    return p.*;
}

/// `*(short*)addr += delta`: the sum is computed in int and stored as short.
fn addShort(addr: usize, delta: c_int) void {
    const p: *align(1) i16 = @ptrFromInt(addr);
    p.* = psx.conv(i16, @as(c_int, p.*) +% delta);
}
