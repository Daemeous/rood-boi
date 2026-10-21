//! Projects a battle actor's world position for the given action code (src/BATTLE/BATTLE.PRG/38C1C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_38C1C");
const c = @import("c_38C1C");

/// orig: func_800A190C (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Writes the position (vy adjusted) of the actor in slot `arg0` for code `arg1` into `arg2`; returns 0, or a negative value when there is none.
pub fn func_800A190C(arg0: c_int, arg1: c_int, arg2: ?*v.SVECTOR, arg3: c_int) callconv(.c) c_int {
    if (arg1 == 0xFB) {
        var a2: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), arg0).*;

        if (a2 == null) {
            a2 = @as(?*v.D_800F4538_t, @ptrCast(psx.at(v.g.D_800F45E0(), arg0).*));
            if (a2 == null) {
                return -1;
            }

            arg2.?.* = a2.?.unk0.position;
            arg2.?.vy = psx.conv(i16, @as(c_int, arg2.?.vy) -% 0xC0);
        } else {
            arg2.?.* = a2.?.unk0.position;
            arg2.?.vy = psx.conv(i16, @as(c_int, arg2.?.vy) -% @as(c_int, a2.?.unk646));
        }

        return 0;
    }

    if (arg1 == 0xFD) {
        // ROOD_PC: an empty slot reads physical address 0 (the PS1 behaviour).
        const slot = psx.at(v.g.D_800F4538(), arg0).*;
        const actor: *align(1) v.D_800F4538_t = if (slot) |s| @as(*align(1) v.D_800F4538_t, s) else psx.lowRam(v.D_800F4538_t);
        arg2.?.* = actor.unk0.position;
        return 0;
    }

    var v0: c_int = undefined;
    if ((arg1 & 0xF0) == 0x40) {
        v0 = arg1;
    } else if (arg1 == 0xFA) {
        const slot = psx.at(v.g.D_800F4538(), arg0).*;
        // ROOD_PC: an empty slot reads physical address 0 (the PS1 behaviour).
        const actor: *align(1) v.D_800F4538_t = if (slot) |s| @as(*align(1) v.D_800F4538_t, s) else psx.lowRam(v.D_800F4538_t);

        arg2.?.* = actor.unk0.position;
        // ONE is 4096.
        const product = @as(c_int, actor.unk65A) *% @as(c_int, actor.unk183C);
        v0 = @divTrunc(product, 4096);
        arg2.?.vy = psx.conv(i16, @as(c_int, arg2.?.vy) +% v0);
        return 0;
    } else {
        v0 = c.func_800A152C(arg0, arg1, 0);

        if (v0 < 0) {
            return v0;
        }
    }

    v0 = c.func_800A1C10(arg0, v0, @ptrCast(arg2), arg3);

    if (v0 < 0) {
        return v0;
    }

    return 0;
}
