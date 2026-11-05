//! The room's background sphere (D2: 2EA3C.s; oracle pc/src/boot_sdk.c).
const c = @import("c_boot_sdk");
const psx = @import("common").psx;

/// orig: func_8009820C (pc/src/boot_sdk.c)
/// Emits the background mesh's triangles at the scratchpad packet cursor
/// (word 0), into the ordering table at word 4 plus 2047 words, projecting
/// through the scratchpad vertex buffer at 0x1F800100; moves the cursor on.
pub fn drawRoomBackground(mesh: [*c]c_uint) callconv(.c) void {
    const cursor: *u32 = @ptrFromInt(0x1f800000);
    const packet: ?[*]c_uint = @ptrFromInt(@as(usize, cursor.*));
    // table + 2047 words in 32-bit address arithmetic, as the C does on a possibly null base.
    const table: [*c]c_uint = @ptrFromInt(@as(usize, @as(*u32, @ptrFromInt(0x1f800004)).*) +% 2047 * 4);
    const end = c.pc_background_packets(mesh, packet, table, @ptrFromInt(0x1f800100));
    cursor.* = psx.addr(end);
}
