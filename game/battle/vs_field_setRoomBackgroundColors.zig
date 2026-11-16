//! Copies the 20-byte scene data block and writes its 33 colours into the background mesh at vs_field_legacyBackgroundGradientPackets (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_setRoomBackgroundColors (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Copies the scene block into vs_field_backgroundGradientColors and writes its words into the packet list.
pub fn vs_field_setRoomBackgroundColors(arg0: [*c]c_int) callconv(.c) void {
    _ = c.vs_main_memcpy(@ptrCast(v.g.vs_field_backgroundGradientColors()), @ptrCast(arg0), @as(c_uint, @sizeOf([5]c_int)));

    const dst = v.g.vs_field_legacyBackgroundGradientPackets();
    var p: usize = 2;
    var a: usize = 0;

    dst[p] = (arg0[a] & 0xFFFFFF);
    a += 1;
    p += 1;

    for (0..8) |_| {
        p += 2;
        dst[p] = (arg0[a] & 0xFFFFFF);
        p += 1;
    }

    a += 1;

    for (0..16) |_| {
        p += 2;
        dst[p] = (arg0[a] & 0xFFFFFF);
        p += 1;
    }

    a += 1;

    for (0..8) |_| {
        p += 2;
        dst[p] = (arg0[a] & 0xFFFFFF);
        p += 1;
    }

    dst[p + 2] = (arg0[a + 1] & 0xFFFFFF);
}
