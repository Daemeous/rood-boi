//! Calls the routine at the room's section 14 with D_800F2258 when both are set (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80093A70 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Calls func_800462E8 with room section 14 and D_800F2258 when both are set.
pub fn func_80093A70() callconv(.c) void {
    if (v.g.D_800F2258().* != -1) {
        // The section14 address is a guest address that need not be aligned for u32: pass its bits as an unaligned pointer, as the C does.
        const raw: u32 = psx.rawOf(v.g.vs_battle_roomData().section14);
        if (raw != 0) {
            const F = *const fn (c_int, ?*anyopaque, c_uint) callconv(.c) void;
            @as(F, @ptrCast(&c.func_800462E8))(
                0x200,
                @ptrFromInt(@as(usize, raw)),
                psx.conv(c_uint, v.g.D_800F2258().*),
            );
        }
    }
}
