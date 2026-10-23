//! Tests whether a point lies within 0x40 of the func_8008D2C0 entry tagged with `arg0 - 2`
//! (src/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A51A0 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Returns 1 if `arg1` lies inside the square of half-width 0x40 around the entry tagged `arg0 - 2`, else 0.
pub fn func_800A51A0(arg0: c_int, arg1: ?*v.SVECTOR) callconv(.c) c_int {
    var sp10: [4]v.func_8008D2C0_t = undefined;
    const want: c_int = arg0 -% 2;
    const n = c.func_8008D2C0(psx.cptr(c.func_8008D2C0_t, &sp10[0]));

    var p: ?*v.func_8008D2C0_t = null;
    var i: c_int = 0;
    while (i < n) : (i +%= 1) {
        const entry = psx.at(&sp10, i);
        if (@as(c_int, entry.unk6) == want) {
            p = entry;
            break;
        }
    }

    // NULL when no entry matched: the C dereferences it, and so does this.
    const pr = p.?;
    const vec = arg1.?;

    if (@as(c_int, pr.unk0) +% 0x40 < @as(c_int, vec.vx)) {
        return 0;
    }
    if (@as(c_int, vec.vx) < @as(c_int, pr.unk0) -% 0x40) {
        return 0;
    }
    if (@as(c_int, pr.unk4) +% 0x40 < @as(c_int, vec.vz)) {
        return 0;
    }
    return @as(c_int, @intFromBool(@as(c_int, vec.vz) >= @as(c_int, pr.unk4) -% 0x40));
}
