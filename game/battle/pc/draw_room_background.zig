//! The room's background sphere (D2: 2EA3C.s; oracle pc/src/boot_sdk.c).
const c = @import("c_boot_sdk");
const scratch = @import("common").scratch;
const pad = @import("common").pad;

/// orig: vs_render_drawSkyDome (pc/src/boot_sdk.c)
/// Emits the background mesh's triangles at the packet cursor
/// into the ordering table's base plus 2047 words, projecting
/// through the scratchpad vertex buffer at 0x1F800100; moves the cursor on.
pub fn drawRoomBackground(mesh: [*c]c_uint, camera: *const @import("common").gte.View) callconv(.c) void {
    const cursor = scratch.get();
    const packet: ?[*]c_uint = @ptrFromInt(@intFromPtr(cursor.unk0));
    // table + 2047 words, as the C does on a possibly null base.
    const table: [*c]c_uint = @ptrFromInt(@intFromPtr(cursor.unk4) +% 2047 * 4);
    const end = c.pc_background_packets(mesh, packet, table, @ptrFromInt(pad.addr(0x100)), @ptrCast(camera));
    cursor.unk0 = end;
}
