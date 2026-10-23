//! zig_diff spec: func_800A6EE8 -> func_800A6EE8.func_800A6EE8 (3A1A0.c).
//! Inputs: an SVECTOR in the arena (vx/vy/vz from edges or random, vy often
//! set so vy - 64 equals the chosen height), arg1/arg2 around 0 and the 128
//! division (some negative), arg3 in 0..7 (every value the C tests), an
//! object-table entry present or absent with bit 17 set or clear and a random
//! low index into D_800E9278 (a third of its entries 0x14). The height
//! functions return a spec-chosen h_pick (zero, negative, 0..16383, any) or a
//! random value; the level function returns 0, h_pick, or a random value, so
//! the `h >= 0`, `los < h` and `h < vy - 64` tests see equal sides.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A6EE8));

var ent_obj: ?*u32 = null;
var ent_present = false;
var ent_bit17 = false;
var h_pick: i16 = 0;

pub const classes = [_][:0]const u8{ "no-ent", "bit17-mode3", "bit17-other", "skip-los", "plain" };

pub const stubs = struct {
    pub fn func_8008DC7C(ctx: *zd.Ctx, x: anytype, z: anytype) i16 {
        _ = x;
        _ = z;
        if (ctx.value(u2) != 0) return h_pick;
        return ctx.value(i16);
    }

    pub fn func_8008DA24(ctx: *zd.Ctx, x: anytype, z: anytype) i16 {
        _ = x;
        _ = z;
        if (ctx.value(u2) != 0) return h_pick;
        return ctx.value(i16);
    }

    pub fn func_800A8D64(ctx: *zd.Ctx, sp: anytype, mode: anytype) ?*u32 {
        _ = ctx;
        _ = sp;
        _ = mode;
        if (!ent_present) return null;
        return ent_obj;
    }

    pub fn func_800B13CC(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(i32);
    }

    pub fn func_800A92B8(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = a;
        _ = b;
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(i32);
    }

    pub fn func_800A9378(ctx: *zd.Ctx, x: anytype, y: anytype, z: anytype, w: anytype) c_int {
        _ = x;
        _ = y;
        _ = z;
        _ = w;
        const k = ctx.value(u3);
        if (k == 0) return 0;
        if (k == 1) return h_pick;
        return ctx.value(i32);
    }
};

fn wide(ctx: *zd.Ctx) c_int {
    const edges = [_]c_int{ 0, -1, 1, 127, 128, -127, -128, -129, 255, 256, 32767, -32768, 65535 };
    if (ctx.chance(2)) return edges[ctx.range(usize, 0, edges.len - 1)];
    return ctx.range(c_int, -200000, 200000);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const sv = ctx.alloc(v.SVECTOR);
    const edges16 = [_]i16{ 0, 1, -1, 127, -128, 32767, -32768 };
    sv.vx = if (ctx.chance(2)) edges16[ctx.range(usize, 0, edges16.len - 1)] else ctx.int(i16);
    sv.vz = if (ctx.chance(2)) edges16[ctx.range(usize, 0, edges16.len - 1)] else ctx.int(i16);

    h_pick = switch (ctx.range(u2, 0, 2)) {
        0 => 0,
        1 => ctx.int(i16),
        else => ctx.range(i16, 0, 16383),
    };
    sv.vy = if (ctx.chance(2)) @truncate(@as(i32, h_pick) + 64) else ctx.int(i16);

    const a1 = wide(ctx);
    const a2 = wide(ctx);
    const mode: c_int = ctx.range(c_int, 0, 7);

    ent_present = !ctx.chance(6);
    ent_bit17 = ctx.chance(2);
    ent_obj = null;
    if (ent_present) {
        const e = ctx.alloc(u32);
        const bit: u32 = if (ent_bit17) (1 << 17) else 0;
        e.* = (ctx.int(u32) & ~@as(u32, 1 << 17)) | bit;
        ent_obj = e;
    }

    const tab = v.g.D_800E9278();
    for (0..32) |i| tab[i] = if (ctx.chance(3)) 0x14 else ctx.int(u8);

    return .{ sv, a1, a2, mode };
}

pub fn classify(args: Args) usize {
    if (!ent_present) return 0;
    if (ent_bit17) return if (args[3] == 3) 1 else 2;
    if (args[3] == 2 or args[3] == 4) return 3;
    return 4;
}
