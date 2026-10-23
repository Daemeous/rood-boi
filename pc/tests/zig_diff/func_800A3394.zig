//! zig_diff spec: func_800A3394 -> func_800A3394.func_800A3394 (3A1A0.c).
//! Inputs: an actor in the arena, placed in a random slot (arg0 0..16) of the
//! D_800F4538 table, with a random menuCameraHeightOffset (edges included). The
//! caller's vector has vy at the i16 edges, so vy - 0xC0 wraps. The floor is
//! absent, equal to the first height test's right-hand side (so the `<` sees
//! equal sides), random, always above the test (both retries fail), or never
//! above it. func_800A3310 reports "blocked" (0) once ctx.calls passes a
//! prepared limit, and sometimes moves the working vector's vy; func_800E75EC
//! returns the prepared floor.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A3394));

pub const classes = [_][:0]const u8{ "no-floor", "floor-equal", "floor-random", "floor-always", "floor-never" };

var floor_value: c_int = 0;
var floor_mode: u8 = 0;
var blocked_until: u32 = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    actor.menuCameraHeightOffset = ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFF });

    const table = v.g.D_800F4538();
    const slot = ctx.range(u5, 0, 16);
    table[slot] = actor;

    const arg = ctx.alloc(v.SVECTOR);
    arg.vx = ctx.int(i16);
    arg.vy = ctx.pick(i16, &.{ -32768, -32704, -32576, -32575, -1, 0, 1, 32767 });
    arg.vz = ctx.int(i16);

    const boundary: c_int = @as(c_int, arg.vy) - 0xC0 - @as(c_int, actor.menuCameraHeightOffset);
    floor_mode = ctx.range(u8, 0, 4);
    floor_value = switch (floor_mode) {
        0 => 0,
        1 => boundary,
        2 => ctx.int(c_int),
        3 => 0x7FFF_FFFF,
        else => -0x8000_0000,
    };
    blocked_until = ctx.range(u32, 0, 4);

    return .{ @as(c_int, slot), arg };
}

pub fn classify(args: Args) usize {
    _ = args;
    return floor_mode;
}

pub const stubs = struct {
    pub fn func_800E75EC(ctx: *zd.Ctx) c_int {
        _ = ctx;
        return floor_value;
    }

    pub fn func_800A3310(ctx: *zd.Ctx, arg0: anytype, pos: anytype) c_int {
        _ = arg0;
        const p: *v.SVECTOR = @ptrCast(@alignCast(pos));
        if (ctx.value(u3) == 0) p.vy = ctx.value(i16);
        if (ctx.calls > blocked_until) return 0;
        const r = ctx.value(c_int);
        return if (r == 0) 1 else r;
    }
};
