//! Passes the four flagged battle camera records to their screen-effect setters
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BDAB4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sends each flagged record of the battle camera block to its screen-effect setter.
pub fn func_800BDAB4() callconv(.c) void {
    // sp10 and sp20 are uninitialised in the original: the callees copy the whole struct, but only the fields written here are read.
    var sp10: v.D_800F1A68_t = std.mem.zeroes(v.D_800F1A68_t);
    var sp20: v.P_CODE = std.mem.zeroes(v.P_CODE);

    if (c.func_800BDBB4(psx.cptr(c.func_800BDBB4_t, &v.g.D_800F4BA4().*.?.unk1FC)) != 0) {
        sp10.unk0 = psx.conv(i32, v.g.D_800F4BA4().*.?.unk1FC.unk2);
        sp10.unk4 = psx.conv(i32, v.g.D_800F4BA4().*.?.unk1FC.unk4);
        c.func_8007DDB8(psx.cptr(c.D_800F1A68_t, &sp10));
    }
    if (c.func_800BDBB4(psx.cptr(c.func_800BDBB4_t, &v.g.D_800F4BA4().*.?.unk21C)) != 0) {
        sp20.r0 = psx.conv(u8, v.g.D_800F4BA4().*.?.unk21C.unk2);
        sp20.g0 = psx.conv(u8, v.g.D_800F4BA4().*.?.unk21C.unk4);
        sp20.b0 = psx.conv(u8, v.g.D_800F4BA4().*.?.unk21C.unk6);
        c.func_8007DDD4(psx.cptr(c.P_CODE, &sp20));
    }
    if (c.func_800BDBB4(psx.cptr(c.func_800BDBB4_t, &v.g.D_800F4BA4().*.?.unk23C)) != 0) {
        sp10.unk0 = psx.conv(i32, v.g.D_800F4BA4().*.?.unk23C.unk2);
        sp10.unk4 = psx.conv(i32, v.g.D_800F4BA4().*.?.unk23C.unk4);
        c.func_8007DDF8(psx.cptr(c.D_800F1A68_t, &sp10));
    }
    if (c.func_800BDBB4(psx.cptr(c.func_800BDBB4_t, &v.g.D_800F4BA4().*.?.unk25C)) != 0) {
        c.pc_screff2_set_warp(
            @as(c_int, v.g.D_800F4BA4().*.?.unk25C.unk2),
            @as(c_int, v.g.D_800F4BA4().*.?.unk25C.unk4),
        );
    }
}
