//! zig_diff spec: func_80078748 -> func_80078748.func_80078748 (146C.c).
//! Inputs: D_800F19CC with a random current slot (unk0, masked & 3) and
//! target; the four tests' stubs return pass or fail per iteration (a list of
//! outcomes drawn in prepare), so each `&&` stops at each place and the call
//! runs when all pass. func_800A0BE0's flags: both bits, either one, or none
//! (with random other bits).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80078748));

pub const classes = [_][:0]const u8{ "busy", "flags", "slot", "not-ready", "enters" };

var outcome: ?*[4]u8 = null;

pub const stubs = struct {
    pub fn func_800CB45C(ctx: *zd.Ctx) c_int {
        return if (outcome.?[0] != 0) 0 else ctx.edge(c_int, &.{ 1, -1, 0x100 });
    }
    pub fn func_800A0BE0(ctx: *zd.Ctx, actor: anytype) c_uint {
        _ = actor;
        const other = ctx.value(c_uint) & ~@as(c_uint, 0x08000100);
        return if (outcome.?[1] != 0) other | 0x08000100 else other | ctx.edge(c_uint, &.{ 0, 0x08000000, 0x100 });
    }
    pub fn vs_battle_getEmptyObjectDataSlot(ctx: *zd.Ctx) c_int {
        return if (outcome.?[2] != 0) 0 else ctx.edge(c_int, &.{ 1, -1 });
    }
    pub fn func_800CEEBC(ctx: *zd.Ctx) c_int {
        return if (outcome.?[3] != 0) ctx.edge(c_int, &.{ 1, -1, 0x10000 }) else 0;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = state;
    outcome = @ptrCast(ctx.allocBytes(4, 1));
    // the first failing test (0..3), or none (4)
    const fail = ctx.range(u8, 0, 4);
    for (0..4) |k| outcome.?[k] = @intFromBool(k != fail);
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    for (0..4) |k| if (outcome.?[k] == 0) return k;
    return 4;
}
