//! Queues a display entry for an actor in the list at D_800F1BAC, rejecting off-screen positions when arg3 bit 7 is set (src/BATTLE/BATTLE.PRG/2842C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_8009406C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Adds an entry for `arg0` to the display list, counting earlier entries for it and keeping the last display length and kind.
pub fn func_8009406C(arg0: c_int, arg1: c_int, arg2: c_int, arg3: c_int) callconv(.c) c_int {
    var sp10: v.SVECTOR = std.mem.zeroes(v.SVECTOR);
    var i: c_int = 0;
    var found: c_int = 0;
    var frame: c_int = 0;
    var kind: c_int = 0;
    const list: ?[*]v.D_800F1BAC_t = @ptrCast(@alignCast(v.g.D_800F1BAC().*));

    while (i < v.g.D_800F227E().*) : (i +%= 1) {
        const e = psx.at(list.?, i);
        if (e.unk8.u8[0] == 2 and @as(c_int, e.unk8.u8[1]) == arg0) {
            frame = @as(c_int, e.displayTextLen) + 0xC;
            if (e.unk6 >= 0) {
                kind = @as(c_int, e.unkB);
                found +%= 1;
            }
        }
    }

    const slot: *v.D_800F1BAC_t = @ptrCast(@alignCast(c.func_800962E4()));
    if ((arg3 & 0x80) != 0) {
        if (c.func_800A1280(arg0, 0xFB, psx.cptr(c.SVECTOR, &sp10), 0) != 0) return 0;
        if (psx.conv(u16, sp10.vx) >= 0x141) return 0;
        if (psx.conv(u16, sp10.vy) >= 0xF1) return 0;
    }
    const arg3_masked = arg3 & 0x7F;

    slot.unk8.u8[0] = 2;
    slot.unk0 = 0;
    slot.unk2 = 0;
    slot.unk4 = 0;
    slot.unk6 = psx.conv(i16, found);
    slot.displayTextLen = if (frame < 0x25) psx.conv(u8, frame) else 0;
    slot.unkB = psx.conv(u8, kind);
    slot.unk8.u8[1] = psx.conv(u8, arg0);
    slot.unkA = psx.conv(u8, arg1);
    slot.unkE = psx.conv(u8, arg3_masked);
    slot.unkC = psx.conv(u16, arg2);

    const count = v.g.D_800F227E();
    count.* = psx.conv(i16, @as(c_int, count.*) +% 1);
    return 0;
}
