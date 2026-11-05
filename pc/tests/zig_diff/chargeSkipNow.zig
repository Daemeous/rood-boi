//! zig_diff spec: charge_skip_now -> charge_skip.now (146C.c, charge_skip.inc).
//! No inputs: pc_charge_skip_state is a stub that returns 0 or 1 and sets its skipping
//! out-parameter to 0, 1 or another nonzero value.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").now));

pub const classes = [_][:0]const u8{ "no-wind-up", "not-skipping", "skipping" };

var in_wind_up: bool = false;
var skipping: c_int = 0;

pub const stubs = struct {
    pub fn pc_charge_skip_state(ctx: *zd.Ctx, phase: [*c]c_int, skip: [*c]c_int, actor: [*c]c_int, action: [*c]c_int) c_int {
        phase[0] = ctx.value(c_int);
        skip[0] = skipping;
        actor[0] = ctx.value(c_int);
        action[0] = ctx.value(c_int);
        return if (in_wind_up) ctx.edge(c_int, &.{ 1, 1, 2, -1 }) else 0;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    in_wind_up = !ctx.chance(3);
    skipping = ctx.edge(c_int, &.{ 0, 0, 1, 5, -1 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (!in_wind_up) return 0;
    return if (skipping == 0) 1 else 2;
}
