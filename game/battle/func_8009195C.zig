//! Looks up an entry in the room's geometry section (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

/// orig: func_8009195C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Returns the address of geometry entry `index` (16 words each, after its count word), or null when there is none.
pub fn func_8009195C(arg0: c_int) callconv(.c) ?*anyopaque {
    const section = psx.get(v.g.vs_battle_roomData().geometrySection) orelse return null;
    const count: c_int = section.*;
    // a1 = section + 1 (int*): 4 bytes on; a1 + arg0 * 16 ints: arg0 * 64 bytes on. Wrapping, as the 32-bit C.
    const base: c_uint = psx.addr(section);
    const off: c_uint = @as(c_uint, @bitCast(arg0 *% 64)) +% 4;
    if (arg0 >= 0 and arg0 < count) {
        return @ptrFromInt(@as(usize, base +% off));
    }
    return null;
}
