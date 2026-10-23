//! zig_diff spec: func_800AA290 -> func_800AA290.func_800AA290 (40564.c).
//! Inputs: a table index 0..16 with the entry null (1 in 8) or an arena actor;
//! a random func_8006EBF8_t_fields (its unk0_0 and unk0_16 bytes give the
//! target); the actor's position with edge values, and sometimes equal to the
//! target on x and z and to the stub's height on y, so the early return and
//! the zero-difference paths run; arg2 at 0, -1 and the other edges; arg3 at 0
//! and the multiplier edges. func_800A6EE8 and vs_gte_rsqrt are recording stubs
//! whose returns are set by prepare (the height may equal the actor's y, the
//! rsqrt may be 0) or drawn with ctx.value. Neither callee writes state.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AA290));

pub const classes = [_][:0]const u8{ "null-actor", "still", "move-arg2-zero", "dist-path", "goto-move", "other-arg2" };

var no_actor: bool = false;
var still: bool = false;
var use_ret: bool = false;
var ret_y: c_int = 0;
var rsqrt_zero: bool = false;

pub const stubs = struct {
    pub fn func_800A6EE8(ctx: *zd.Ctx, target: anytype, a: anytype, b: anytype, c: anytype) c_int {
        _ = target;
        _ = a;
        _ = b;
        _ = c;
        if (use_ret) return ret_y;
        return ctx.value(c_int);
    }
    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        if (rsqrt_zero) return 0;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    use_ret = false;
    still = false;
    rsqrt_zero = ctx.chance(4);
    no_actor = ctx.chance(8);
    const idx: c_int = ctx.range(c_int, 0, 16);
    const arg2 = ctx.pick(c_int, &.{ 0, -1, 1, 2, 65535, -2, 0x10000, 0x7FFFFFFF, -0x80000000 });
    const arg3 = ctx.pick(c_int, &.{ 0, 1, -1, 128, 127, 0x7FFFFFFF, -0x80000000 });
    const actor = ctx.alloc(v.D_800F4538_t);
    const field = ctx.alloc(v.func_8006EBF8_t_fields);
    v.g.D_800F4538()[@intCast(idx)] = if (no_actor) null else actor;

    const p = &actor.unk0.position;
    p.vx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    p.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    p.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    const tx: i16 = @intCast(@as(c_int, psx.getBits(field, "unk0_0")) * 128 + 64);
    const tz: i16 = @intCast(@as(c_int, psx.getBits(field, "unk0_16")) * 128 + 64);
    const eq_x = ctx.chance(4);
    const eq_z = ctx.chance(4);
    if (eq_x) p.vx = tx;
    if (eq_z) p.vz = tz;
    if (ctx.chance(4)) {
        use_ret = true;
        ret_y = p.vy;
    } else if (ctx.chance(3)) {
        use_ret = true;
        ret_y = ctx.pick(c_int, &.{ 0, 1, -1, 1000, -1000, 0x7FFFFFFF, -0x80000000 });
    }
    still = use_ret and eq_x and eq_z and ret_y == @as(c_int, p.vy);

    return .{ idx, field, arg2, arg3 };
}

pub fn classify(args: Args) usize {
    if (no_actor) return 0;
    if (still) return 1;
    if (args[2] == 0) return 2;
    if (args[2] == -1) return if (rsqrt_zero or args[3] == 0) 4 else 3;
    return 5;
}
