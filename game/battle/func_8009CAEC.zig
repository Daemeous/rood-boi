//! Picks the sub-object pointer and the index for slot `arg1` from the id tables (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_8009CAEC (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Stores the table pointer for the actor's id in slot `arg1`, then the table index for that id.
pub fn func_8009CAEC(arg0: ?*v.D_800F4538_t, arg1: c_int) callconv(.c) void {
    const s = arg0.?;
    const ids = psx.at(v.g.D_800F4628(), arg1);
    const names = psx.at(v.g.D_800F4810(), arg1);
    const values = psx.at(v.g.D_800F4938(), arg1);

    var i: c_int = 0;
    var p: ?*align(1) anyopaque = null;
    var hit = false;
    while (true) {
        const id: c_int = @as(c_int, psx.at(ids, i).*);
        if (id == -1) break;
        if (id == @as(c_int, psx.at(v.g.D_800E8960(), s.unk6E6).*) or id == @as(c_int, s.unk6E6)) {
            // char* p = entry + 8; a null entry gives address 8, as the C does.
            const entry: ?*u8 = psx.at(names, i).*;
            p = @ptrFromInt(@as(usize, (if (entry) |q| psx.addr(q) else 0) +% 8));
            hit = true;
            break;
        }
        i +%= 1;
    }
    if (hit) psx.put(psx.at(&s.unk5DC, arg1), p);

    // tail: the index of the id in this slot's table.
    i = 0;
    while (true) {
        const id: c_int = @as(c_int, psx.at(ids, i).*);
        if (id == -1) break;
        if (id == @as(c_int, s.unk6E6)) {
            psx.at(&s.unk183E, arg1).* = psx.conv(u8, psx.at(values, i).*);
            return;
        }
        i +%= 1;
    }
    psx.at(&s.unk183E, arg1).* = 0xFF;
}
