//! zig_diff spec: pc_charge_skip_hold -> charge_skip.hold (146C.c, charge_skip.inc).
//! Inputs: the name window text allocated or not, the skip decision (charge_skip_now stub),
//! the name setting (0, 0.5, 1, 2, 10 s), the hold already shown (a stub: 0, 30, 90, 600
//! vsyncs), the game's countdown D_800F4E68 (0, 1, 59, 60, 120, 255), Ashley or another as caster.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").hold));

pub const classes = [_][:0]const u8{ "no-window", "not-skipping", "none-left", "capped", "held" };

var now_result: c_int = 0;
var shown: c_int = 0;

pub const stubs = struct {
    pub fn charge_skip_now(ctx: *zd.Ctx) c_int {
        _ = ctx;
        return now_result;
    }
    pub fn charge_skip_hold_vsyncs(ctx: *zd.Ctx, caster: c_int) c_int {
        _ = ctx;
        return shown + @as(c_int, @intFromBool(caster != 0));
    }
    pub fn pc_vsync_count(ctx: *zd.Ctx) c_uint {
        _ = ctx;
        return 4242;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    now_result = ctx.edge(c_int, &.{ 0, 1, 1, 1, 3 });
    shown = ctx.edge(c_int, &.{ 0, 0, 30, 90, 600 });
    v.g.nameWindowText().* = if (ctx.chance(5)) null else @ptrCast(@alignCast(ctx.allocBytes(128, 16)));
    v.g.pc_qol_skip_charging_name().* = ctx.edge(f32, &.{ 0.0, 0.5, 1.0, 2.0, 10.0, 0.008, 0.009, 3.0e9, -3.0e9, std.math.nan(f32) });
    v.g.nameWindowTimer().* = ctx.edge(u8, &.{ 0, 1, 59, 60, 61, 120, 255 });
    const block = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = block;
    block.unk0 = ctx.int(u32);
    block.unk854[block.unk0 & 3].unk4.unk0.targetActor = if (ctx.chance(2)) 0 else ctx.edge(u8, &.{ 1, 255 });
    v.g.charge_skip_held().* = ctx.edge(c_int, &.{ 0, 1, 7 });
    v.g.charge_skip_held_at().* = ctx.int(c_uint);
    v.g.charge_skip_held_vsyncs().* = ctx.int(c_int);
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.nameWindowText().* == null) return 0;
    if (now_result == 0) return 1;
    const block = v.g.D_800F19CC().*.?;
    const caster = block.unk854[block.unk0 & 3].unk4.unk0.targetActor;
    const name = v.g.pc_qol_skip_charging_name().*;
    const f = name * 60.0 + 0.5;
    var vsyncs: c_int = if (f > -2147483648.0 and f < 2147483648.0) @intFromFloat(f) else -2147483648;
    vsyncs -%= shown + @as(c_int, @intFromBool(caster != 0));
    if (vsyncs > v.g.nameWindowTimer().*) {
        vsyncs = v.g.nameWindowTimer().*;
        return if (vsyncs <= 0) 2 else 3;
    }
    return if (vsyncs <= 0) 2 else 4;
}
