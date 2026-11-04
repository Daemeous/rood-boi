//! Armed-reaction pick (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800E4764 (pc/src/battle_movement.c)
/// Picks the first armed reaction from the packed bytes at 0x224: flag 0x20
/// (code 10) then 0x10 (code 9) when byte 1 is set, the reverse otherwise.
/// Low byte flags select byte 2, byte 1 flags select byte 3.
pub fn pickReaction(state: [*]u8, value: *c_int, code: *c_int) callconv(.c) c_int {
    const packed_word = @as(*align(1) const u32, @ptrCast(state + 0x224)).*;
    const low = packed_word & 0xff;
    const high = (packed_word >> 8) & 0xff;
    const bits = [2]c_uint{ 0x20, 0x10 };
    const codes = [2]c_int{ 10, 9 };
    var order: usize = if (state[1] != 0) 0 else 1;
    var pass: c_int = 0;
    while (pass < 2) : ({
        pass += 1; // pass < 2
        order ^= 1;
    }) {
        const bit = bits[order];
        if ((low & bit) != 0 or (high & bit) != 0) {
            value.* = @bitCast(if ((low & bit) != 0) (packed_word >> 16) & 0xff else packed_word >> 24);
            code.* = codes[order];
            return 1;
        }
    }
    // With byte 1 set the original keeps alternating between both tests
    // forever; with it clear it returns 0 after the 0x10 then 0x20 tests.
    if (state[1] != 0) c.pc_unimplemented("func_800E4764 endless reaction scan");
    return 0;
}
