//! Copies a battle block to another and relocates its two guest pointers to the copy (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009C378 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Copies 0x7800 bytes from arg1 to arg0, then adds the copy's displacement to the two guest pointers at its start.
pub fn func_8009C378(arg0: ?*v.func_8009C378_t, arg1: ?*v.func_8009C378_t) callconv(.c) void {
    c.vs_main_memcpy(arg0, arg1, 0x7800);

    // The guest address fields are updated by their 4-byte raw value, as psx_raw does: the field is
    // a 4-byte guest pointer on every target, so it is stored as a u32 at its own address.
    const delta: u32 = psx.addr(arg0.?) -% psx.addr(arg1.?);
    const raw0: *align(1) u32 = @ptrCast(&arg0.?.unk0);
    const raw4: *align(1) u32 = @ptrCast(&arg0.?.unk4);
    raw0.* +%= delta;
    raw4.* +%= delta;
}
