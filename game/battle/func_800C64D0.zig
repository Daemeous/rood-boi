//! Walks a chain of guest words to the word linking back to arg0 - 1, then
//! relinks it to arg1 (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;

/// orig: func_800C64D0 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Follows the 24-bit link chain from arg0 to the word whose link is arg0 - 1, then writes arg1's address into that word's link and the chain's link into arg1's word.
pub fn func_800C64D0(arg0: [*c]c_uint, arg1: [*c]c_int) callconv(.c) void {
    const mask: u32 = 0xFFFFFF;
    const arg0_addr: u32 = psx.addr(arg0);
    // psx_addr(arg0 - 1) & a3: arg0 - 1 is one u_long back, 4 bytes.
    const temp_t0: u32 = (arg0_addr -% 4) & mask;

    var cur: u32 = arg0_addr;
    while ((@as(*align(1) const u32, @ptrFromInt(@as(usize, cur))).* & mask) != temp_t0) {
        // The link is a 24-bit guest address; 0x80000000 makes it a kseg0 address (wraps as in C).
        cur = (@as(*align(1) const u32, @ptrFromInt(@as(usize, cur))).* & mask) | 0x80000000;
    }

    const arg1_addr: u32 = psx.addr(arg1);
    const head: *align(1) u32 = @ptrFromInt(@as(usize, cur));
    head.* = (head.* & 0xFF000000) | (arg1_addr & mask);

    // Reads *arg1 after the store above, as the C does (arg1 may alias the head word).
    const slot: *align(1) u32 = @ptrFromInt(@as(usize, arg1_addr));
    slot.* = (slot.* & 0xFF000000) | temp_t0;
}
