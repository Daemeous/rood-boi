//! Biased position and fill bytes of vs_field_triggerMarkerVertex, then the call to vs_render_drawRoomGeometry on vs_field_triggerMarkerRecordTail - 0x58 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_drawRoomTriggerMarker (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores a 0x80-biased position and a fill byte into vs_field_triggerMarkerVertex, then calls vs_render_drawRoomGeometry on the block at vs_field_triggerMarkerRecordTail - 0x58.
pub fn vs_field_drawRoomTriggerMarker(arg0: c_int, arg1: c_int, arg2: c_short, arg3: c_uint) callconv(.c) void {
    const block = v.g.vs_field_triggerMarkerVertex();
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

    c.vs_render_drawRoomGeometry(@ptrCast(psx.at(v.g.vs_field_triggerMarkerRecordTail(), @as(c_int, -0x58))), v.g.D_800F19C8().*);
}
