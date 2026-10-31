//! Per-entry callbacks over the battle table D_800F1DD8: the first 64 entries
//! call a handler chosen by kind, the next entries run a timed step and free
//! themselves when it finishes (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80091B04 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Runs the kind-dispatched handlers of the first entries, then the timed steps of the next ones, freeing those that finish.
pub fn func_80091B04() callconv(.c) void {
    const Handler = *const fn (?*v.D_800F1DD8_t2) callconv(.c) c_int;

    var i: c_int = 0;
    while (i < @as(c_int, v.g.D_800F225A().*)) : (i +%= 1) {
        const e = &v.g.D_800F1DD8()[@as(usize, @intCast(i))];
        if (e.unk0 != 0 and e.unk1 < 7 and e.unk2 != 0) {
            const handler: Handler = @ptrCast(v.g.D_800E85CC()[e.unk1].?);
            e.unk0 = psx.conv(u8, handler(psx.get(e.unk4)));
        }
    }

    i = 0;
    while (i < @as(c_int, v.g.D_800F225B().*)) : (i +%= 1) {
        const e = &v.g.D_800F1DD8()[@as(usize, @intCast(i + 64))];
        if (e.unk0 != 0 and e.unk2 != 0) {
            e.unk0 = psx.conv(u8, c.func_80092B04(
                @ptrCast(psx.get(e.unk4)),
                @as(c_int, e.unk1),
                @ptrCast(e),
            ));
            if (e.unk0 == 0) {
                c.vs_main_freeHeap(psx.get(e.unk4));
                // psx_copy: the raw guest address of unk8 becomes unk4 (same field type, bitwise copy).
                e.unk4 = e.unk8;
            }
        }
    }
}
