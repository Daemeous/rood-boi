//! Steps a timed interpolation and writes its current position
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BDBB4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Advances a timed interpolation by half a tick, writes its position, and returns 1 while it runs (0 when idle).
pub fn func_800BDBB4(arg0: ?*v.func_800BDBB4_t) callconv(.c) c_int {
    const p = arg0.?;
    if (p.unk1E == 0) {
        return 0;
    }

    p.unk1C = psx.conv(i16, @as(c_int, p.unk1C) +% (v.g.vs_gametime_tickspeed().* >> 1));

    const v0: c_int = c.func_800BFEBC(@as(c_int, p.unk1), @as(c_int, p.unk1C), @as(c_int, p.unk1E));
    // Scratchpad 0x1F800088: the factor is re-read from memory after each store, as the C does.
    const a0: *v.func_800BDBB4_t2 = @ptrFromInt(0x1F800088);
    a0.unk0 = v0;
    p.unk2 = psx.conv(i16, @as(c_int, p.unkA) +% ((@as(c_int, p.unk12) *% a0.unk0) >> 12));
    p.unk4 = psx.conv(i16, @as(c_int, p.unkC) +% ((@as(c_int, p.unk14) *% a0.unk0) >> 12));
    p.unk6 = psx.conv(i16, @as(c_int, p.unkE) +% ((@as(c_int, p.unk16) *% a0.unk0) >> 12));

    if (p.unk1C == p.unk1E) {
        p.unk1E = 0;
    }
    return 1;
}
