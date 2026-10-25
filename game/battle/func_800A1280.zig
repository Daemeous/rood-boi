//! Projects a battle slot's position to screen space, or looks up an entry
//! for any other code (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_800A1280 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Code 0xFB: writes the slot's screen position into `out`; other codes: looks up `arg1` for slot `arg0` and passes it on.
pub fn func_800A1280(arg0: c_int, arg1: c_int, arg2: ?*v.SVECTOR, arg3: c_int) callconv(.c) c_int {
    if (arg1 == 0xFB) {
        var sp10: c_int = undefined;
        var sp14: c_int = undefined;
        var sp18: c_int = undefined;

        const actor = psx.at(v.g.D_800F4538(), arg0).*;
        if (actor) |a| {
            if (psx.getBits(&a.unk0, "unkB_0") != 0) return -1;
            arg2.?.vx = a.unk0.position.vx;
            arg2.?.vz = a.unk0.position.vz;
            arg2.?.vy = psx.conv(i16, @as(c_int, a.unk0.position.vy) -% @as(c_int, a.unk646));
        } else {
            const slot = psx.at(v.g.D_800F45E0(), arg0).* orelse return -1;
            arg2.?.vx = slot.unk1C;
            arg2.?.vz = slot.unk20;
            arg2.?.vy = psx.conv(i16, @as(c_int, slot.unk1E) -% 0xC0);
        }

        const rot: *v.MATRIX = @ptrFromInt(0x1F800014);
        c.SetRotMatrix(psx.cptr(c.MATRIX, rot));
        c.SetTransMatrix(psx.cptr(c.MATRIX, rot));

        // PC_GTE_UNNARROWED: the projection runs with the widescreen anchor raised.
        v.g.pc_gte_anchored().* +%= 1;
        arg2.?.vz = psx.conv(i16, c.RotTransPers(psx.cptr(c.SVECTOR, arg2.?), &sp10, &sp14, &sp18));
        v.g.pc_gte_anchored().* -%= 1;

        arg2.?.vx = psx.conv(i16, sp10);
        arg2.?.vy = psx.byteAt(i16, &sp10, 2).*;

        return 0;
    }

    const var_v0 = c.func_800A152C(arg0, arg1, 0);
    if (var_v0 < 0) {
        return var_v0;
    }

    // psx_addr(arg2): NULL is address 0.
    const addr: c_int = if (arg2) |p| @bitCast(psx.addr(p)) else 0;
    const v1 = c.func_800A141C(arg0, var_v0, addr, arg3);
    if (v1 < 0) {
        return v1;
    }
    return 0;
}
