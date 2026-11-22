//! Biased position and fill bytes of the marker record's vertex block, then the call to vs_render_drawRoomGeometry on the marker's record (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

// The record is the decomp's labels' bytes in one object (vs_battle_mapCompletionFlags[16..]): its members lie where the labels of the matrix (+0x24), the vertex block
// (+0x4c; its colour rows, +0x58, are the label the call's address was taken from) and the texture source (+0x74) did.
comptime {
    if (@sizeOf(v.vs_field_triggerMarkerBlock_t) != 0x7c or @offsetOf(v.vs_field_triggerMarkerBlock_t, "matrix") != 0x24 or
        @offsetOf(v.vs_field_triggerMarkerBlock_t, "vertex") != 0x4c or @offsetOf(v.vs_field_triggerMarkerBlock_t, "textureSource") != 0x74)
        @compileError("the trigger marker record's layout is not the decomp's");
}

/// orig: vs_field_drawRoomTriggerMarker (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores a 0x80-biased position and a fill byte into the marker record's vertex block, then calls vs_render_drawRoomGeometry on the marker's record (the tail of vs_battle_mapCompletionFlags).
pub fn vs_field_drawRoomTriggerMarker(arg0: c_int, arg1: c_int, arg2: c_short, arg3: c_uint) callconv(.c) void {
    const record: *v.vs_field_triggerMarkerBlock_t = @ptrCast(&v.g.vs_battle_mapCompletionFlags()[16]); // the record's header is the tail of vs_battle_mapCompletionFlags
    const block = &record.vertex;
    block.unk0 = psx.conv(i16, arg0 +% 0x80);
    block.unk4 = psx.conv(i16, arg1 +% 0x80);
    block.unk2 = arg2;

    // The C stores arg3 into char bytes: only the low byte survives.
    const fill = psx.conv(u8, arg3);
    // Three rows of unkC, each with its first three bytes written (row[3] is untouched).
    for (&block.unkC) |*row| {
        row[0] = fill;
        row[1] = fill;
        row[2] = fill;
    }
    // The C's `var_t0 += 3` after the loop lands on unk24.
    block.unk24[0] = fill;
    block.unk24[1] = fill;
    block.unk24[2] = fill;

    c.vs_render_drawRoomGeometry(@ptrCast(record), v.g.D_800F19C8().*);
}
