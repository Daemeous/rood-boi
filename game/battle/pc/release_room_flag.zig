//! Releasing an actor's room flag (D2: 7E05C.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: func_800E6EB0 (pc/src/battle_movement.c)
/// When actor `id` is of type 11 or 14, its room-flag word (0x170, bits 8-15)
/// is set and its model byte 0xC has a zero low nibble, clears that room
/// flag. A missing record reads PS1 low RAM, as the original does.
pub fn releaseActorRoomFlag(id: c_int) callconv(.c) void {
    const actor = psx.at(v.g.vs_battle_actors(), id).*;
    const state_slot = psx.at(v.g.D_800F5878(), id).*;
    if (actor == null and state_slot == null) return;
    const low_ram: [*]u8 = @ptrCast(psx.lowRam(u8));
    const state: [*]u8 = if (state_slot) |s| @ptrCast(s) else low_ram;
    const kind: c_uint = if (actor) |a| @as([*]u8, @ptrCast(a))[9] else low_ram[9];
    if (((@as(c_uint, 1) << @intCast(kind & 31)) & 0x4800) == 0) return;
    const flags: c_uint = psx.byteAt(c_uint, state, 0x170).* & 0xff00;
    const model: [*]u8 = @ptrFromInt(psx.byteAt(c_uint, state, 0x58).*);
    if (flags == 0 or (model[0xc] & 0xf) != 0) return;
    c.func_800E4BD8(@bitCast(flags >> 8));
}
