//! Takes the next byte from a counted byte queue (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");

/// orig: func_800BB788 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Pops the next byte of a counted queue, or returns 1 when the count is zero.
pub fn func_800BB788(arg0: ?*v.func_800BB68C_t) callconv(.c) c_ushort {
    const q = arg0.?;
    if (q.unk0 == 0) {
        return 1;
    }

    q.unk0 -%= 1;
    // The index is the read position before the increment; it may pass the 22-byte array's end, as the C does.
    const idx: c_uint = q.unk1;
    q.unk1 +%= 1;
    return psx.conv(c_ushort, psx.at(&q.unk2, idx).*);
}
