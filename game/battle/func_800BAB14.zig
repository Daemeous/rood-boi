//! Effect slot dispatch for a script packet: arg0[1] selects a template or a slot
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BAB14 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Dispatches the packet's slot byte: 0xFF clears through func_800434A4, bit 3 copies a template into the scratch block first, otherwise the slot's block is used directly.
pub fn func_800BAB14(arg0: [*c]u8) callconv(.c) c_int {
    const a0: c_int = arg0[1];
    if (a0 == 0xFF) {
        _ = c.func_800434A4(0, 2);
    } else if ((a0 & 8) != 0) {
        c.vs_main_memcpy(v.g.D_800F4C48().*, v.g.D_800F4BC0()[@intCast(a0 & 7)], 0x80);
        // D_800F4C48 + 8: void* arithmetic in bytes, as GNU C does it.
        const base = v.g.D_800F4C48().*;
        _ = c.func_800433B4(@ptrFromInt(@intFromPtr(base) +% 8), 0xFF, 2);
    } else {
        const entry = v.g.D_800F4BC0()[@intCast(a0)];
        if (entry != null) {
            _ = c.func_800433B4(@ptrFromInt(@intFromPtr(entry) +% 8), 0xFF, 2);
        }
    }
    return 0;
}
