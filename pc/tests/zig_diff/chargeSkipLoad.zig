//! zig_diff spec: charge_skip_load -> charge_skip.load (146C.c, charge_skip.inc).
//! The effect loader's state (D_800F531C) starts at each state the loop runs on (6, 2, 8, 3) or
//! not (5, 0, 7); the loader stub moves it on after `steps` of its calls (never, for the 64
//! step limit) and repoints the effect file record (D_800F569C), which the C puts back.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").load));

pub const classes = [_][:0]const u8{ "not-loading", "steps", "limit-64" };

var steps: u32 = 0;
var other: ?*v.D_800F569C_t = null;

pub const stubs = struct {
    pub fn func_800D7890(ctx: *zd.Ctx, arg: c_int) void {
        _ = arg;
        v.g.D_800F569C().* = other;
        _ = ctx;
    }
    pub fn func_800CE9B0(ctx: *zd.Ctx) c_int {
        // three stub calls a step: this is call 3 * i + 2 (func_800D7890 came first)
        if (ctx.calls / 3 + 1 >= steps) {
            v.g.D_800F531C().* = ctx.edge(c_uint, &.{ 5, 5, 0, 7, 1 });
        } else {
            v.g.D_800F531C().* = ctx.edge(c_uint, &.{ 6, 2, 8, 3 });
        }
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F531C().* = ctx.edge(c_uint, &.{ 6, 2, 8, 3, 6, 2, 8, 3, 5, 0, 7, 1 });
    steps = if (ctx.chance(6)) 1000 else ctx.range(u32, 1, 70);
    v.g.D_800F569C().* = if (ctx.chance(4)) null else ctx.alloc(v.D_800F569C_t);
    other = if (ctx.chance(4)) null else ctx.alloc(v.D_800F569C_t);
    v.g.D_800F568C().* = if (ctx.chance(4)) null else ctx.alloc(v.vs_main_CdQueueSlot);
    v.g.pc_charge_skip_pumped().* = ctx.int(c_int);
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const s = v.g.D_800F531C().*;
    if (s != 6 and s != 2 and s != 8 and s != 3) return 0;
    return if (steps >= 64) 2 else 1;
}
