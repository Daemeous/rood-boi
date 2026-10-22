//! Builds the target-selection call for a battle action and, when it reports a hit, runs the follow-up at `arg1`
//! (src/BATTLE/BATTLE.PRG/74088.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_74088");
const c = @import("c_74088");

/// orig: func_800DCAA0 (src/BATTLE/BATTLE.PRG/74088.c)
/// Runs the target check for `source` against `state` (mode 3 also tests the actor's height) and calls func_800DC888 with `arg1` on success.
pub fn func_800DCAA0(arg0: ?*v.func_800DCAA0_t1, arg1: c_int, arg2: ?*v.func_800DCAA0_t, arg3: c_int) callconv(.c) void {
    const a0 = arg0.?;
    const a2 = arg2.?;
    var sp20: v.SVECTOR = undefined;
    var sp28: v.SVECTOR = undefined;
    const temp_s0: c_int = @as(c_int, psx.getBits(a2, "unkC_0"));
    var temp_v0: c_int = undefined;

    if (arg3 != 3) {
        temp_v0 = c.func_8008631C(
            @as(c_int, psx.getBits(a2, "unk4_5")),
            @as(c_int, a0.unk88),
            temp_s0,
            @as(c_int, psx.getBits(a2, "unkC_4")),
            v.g.D_800F5900().*,
        );
    } else {
        const actor = v.g.D_800F4538()[@intCast(temp_s0)].?;
        sp28.vx = actor.unk0.position.vx;
        sp28.vz = actor.unk0.position.vz;
        sp28.vy = psx.conv(i16, @as(c_int, actor.unk0.position.vy) -% @as(c_int, psx.getBits(a2, "unkC_7")));
        sp20.vx = sp28.vx;
        sp20.vy = sp28.vy;
        sp20.vz = psx.conv(i16, @as(c_int, sp28.vz) +% psx.conv(c_int, c.vs_gte_rsqrt(a2.unk8)));
        temp_v0 = c.func_800863A4(
            @as(c_int, psx.getBits(a2, "unk4_5")),
            @as(c_int, a0.unk88),
            temp_s0,
            @as(c_int, psx.getBits(a2, "unkC_4")),
            psx.cptr(c.SVECTOR, &sp20),
            psx.cptr(c.SVECTOR, &sp28),
            v.g.D_800F5900().*,
        );
    }
    if (temp_v0 != 0) {
        // psx_to_ptr(void*, arg1): any address, aligned or not (the C passes it through
        // untyped); the call goes through the callee typed with an untyped pointer.
        const apply: *const fn (?*anyopaque) callconv(.c) void = @ptrCast(&c.func_800DC888);
        apply(@ptrFromInt(@as(usize, psx.conv(u32, arg1))));
    }
}
