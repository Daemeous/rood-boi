//! Steps a timed interpolation and writes its scale into scratchpad 0x1F800088
//! and its offset into the record (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BE01C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Advances the record's timer by half a tick and sets its scale and offset from its curve.
pub fn func_800BE01C(arg0: ?*v.func_800BDF6C_t) callconv(.c) c_int {
    const e = arg0.?;
    const temp_s1: c_int = @as(c_int, e.unk4) << 12;

    if (e.unk9 == 0) return 0;

    e.unk8 = psx.conv(u8, @as(c_int, e.unk8) +% (v.g.vs_gametime_tickspeed().* >> 1));

    if (e.unk9 < e.unk8) e.unk8 = e.unk9;

    const scratch: *c_int = @ptrFromInt(0x1F800088);
    const unk8: c_int = e.unk8;
    const unk9: c_int = e.unk9;

    switch (e.unk0.u8[1]) {
        0 => {
            scratch.* = 0x1000;
        },
        1 => {
            scratch.* = c.rsin(@divTrunc(unk8 << 10, unk9) +% 0x400);
        },
        2 => {
            scratch.* = c.rsin(@divTrunc(unk8 << 11, unk9));
        },
        3 => {
            scratch.* = c.rsin(@divTrunc(unk8 << 10, unk9));
        },
        else => {},
    }

    // The C reads the scratchpad scale after the rsin call above.
    const sinv: c_int = c.rsin(@divTrunc(temp_s1 *% unk8, unk9));
    const amp: c_int = (sinv *% @as(c_int, e.unk6)) >> 12;
    e.unk2 = psx.conv(i16, (amp *% scratch.*) >> 12);

    if (e.unk8 == e.unk9) e.unk9 = 0;
    return 1;
}
