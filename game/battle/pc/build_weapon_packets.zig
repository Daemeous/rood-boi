//! Weapon polygon packets (D2: 4B16C.s; oracle pc/src/weapon_packets.c).
const c = @import("c_weapon_packets");
const psx = @import("common").psx;

const NativeDraw = *const fn (c_int, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque, ?[*]c_uint) callconv(.c) void;

/// orig: func_800B3CF0 (pc/src/weapon_packets.c)
/// Builds the weapon's packets from the projected vertices (screen words,
/// then depths 0xC00 bytes on) and the model's polygon records at model
/// +0x3C, coloured by the word at `color`, with the packet cursor and
/// ordering table from the scratchpad's words 0 and 4 and the texture word
/// at 0x1F800390; moves the cursor on and hands the new packets to the
/// native renderer.
pub fn buildWeaponPackets(projected: [*]c_uint, object: [*]u8, color: [*]const c_uint, model: [*]u8) callconv(.c) void {
    // The cursor, table and polygon words are guest addresses the C uses as-is (any alignment).
    @setRuntimeSafety(false);
    const texture = @as(*c_uint, @ptrFromInt(0x1f800390)).*;
    const scratch: *u32 = @ptrFromInt(0x1f800000);
    const start: ?[*]c_uint = @ptrFromInt(@as(usize, scratch.*));
    const ot: ?[*]c_uint = @ptrFromInt(@as(usize, @as(*u32, @ptrFromInt(0x1f800004)).*));
    const polygons: ?[*]const c_uint = @ptrFromInt(@as(usize, psx.byteAt(u32, model, 0x3c).*));
    const end = c.pc_weapon_packets(start, ot, projected, projected + 0x300, polygons, @ptrCast(@alignCast(object + 2)), color[0], texture & 0xffff, texture >> 16);
    scratch.* = psx.addr(end);
    if (psx.weak(NativeDraw, "pc_object_native_draw")) |draw| draw(0, projected, object, model, color, start);
}
