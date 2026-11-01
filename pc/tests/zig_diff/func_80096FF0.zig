//! zig_diff spec: func_80096FF0 -> func_80096FF0.func_80096FF0 (2842C.c).
//! Inputs: the abilityTimingDisplay setting (on 7 times in 8), the stub
//! func_800A1280's result (0 or not) and the point it writes into
//! D_800F2280: vy below 40 often (-32768 included), vx placed so that
//! vx + (40 - vy) is 288 exactly, one either side, or random. arg0 at its
//! edges (the i16 timer wraps).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80096FF0));

pub const classes = [_][:0]const u8{ "off", "no-point", "low-right", "low-left", "high" };

/// What the stub hands back this iteration (chosen in prepare, so classify sees it).
var point: v.SVECTOR = undefined;
var failed: c_int = 0;

pub const stubs = struct {
    pub fn func_800A1280(ctx: *zd.Ctx, actor: anytype, limb: anytype, out: anytype, mode: anytype) c_int {
        _ = ctx;
        _ = actor;
        _ = limb;
        _ = mode;
        const p: *v.SVECTOR = @ptrCast(out);
        p.* = point;
        return failed;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    psx.setBits(v.g.vs_main_settings(), "abilityTimingDisplay", @intFromBool(!ctx.chance(8)));
    failed = if (ctx.chance(8)) ctx.int(c_int) | 1 else 0;
    const vy: i16 = switch (ctx.range(u8, 0, 7)) {
        0 => -32768,
        1 => 40,
        2 => 39,
        3 => 41,
        4 => ctx.int(i16),
        else => ctx.range(i16, -16, 47),
    };
    const d: i32 = 40 - @as(i32, vy);
    point = .{
        .vx = switch (ctx.range(u8, 0, 7)) {
            0, 1, 2 => @truncate(288 - d),
            3 => @truncate(287 - d),
            4 => @truncate(289 - d),
            else => ctx.int(i16),
        },
        .vy = vy,
        .vz = ctx.int(i16),
        .pad = ctx.int(i16),
    };
    return .{ctx.pick(c_int, &.{ 0, 1, -1, 32767, -32768, 0x7FFF_FFFF, -2147483648 })};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (psx.getBits(v.g.vs_main_settings(), "abilityTimingDisplay") == 0) return 0;
    if (failed != 0) return 1;
    if (point.vy >= 40) return 4;
    const d: i32 = 40 - @as(i32, point.vy);
    return if (@as(i32, point.vx) + d < 288) 2 else 3;
}
