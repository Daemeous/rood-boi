//! zig_diff spec: func_800B7B58 -> func_800B7B58.func_800B7B58 (4A0A8.c).
//! Inputs: a 16-byte arg0 buffer in the arena whose byte 3 (the `& 1` test)
//! takes values with and without the sign bit, and whose byte 9 is 0xFF
//! about a third of the time (the sentinel) or 0xFE/0x00/0x01/0x7F otherwise;
//! D_800F4C2C is 2 half the time (the other branch otherwise); arg1 is random.
//! The getShort values are random shorts, so the >> 1 and the i16 adds wrap.
//! Stubs: func_800BFE50 returns an index below 24 (the table room), a third of
//! the time with bits above 16 set that the C's u_short drops; func_800A1108
//! writes the vector the C adds to, func_800A9D90 records the vector into a
//! spec sink in the arena, and func_8009DC8C writes D_800F4B70.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B7B58));

pub const classes = [_][:0]const u8{ "set-vector", "add-sentinel", "add-explicit", "sel2" };

/// The SVECTOR func_800A9D90 was last given (set by prepare each iteration).
var sink: *v.SVECTOR = undefined;

pub const stubs = struct {
    pub fn func_800BFE50(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        const actor: c_int = ctx.value(u8) % 24;
        return if (ctx.value(u8) % 3 == 0) actor | (@as(c_int, ctx.value(u8)) << 16) else actor;
    }

    pub fn func_800A1108(ctx: *zd.Ctx, id: anytype, out: anytype) c_int {
        _ = id;
        const p: *anyopaque = out.?;
        const s: *v.func_8006EBF8_t = @ptrCast(@alignCast(p));
        s.unk0.unk4.vx = ctx.value(i16);
        s.unk0.unk4.vy = ctx.value(i16);
        s.unk0.unk4.vz = ctx.value(i16);
        return ctx.value(c_int);
    }

    pub fn func_800A9D90(ctx: *zd.Ctx, id: anytype, vec: anytype, mode: anytype) void {
        _ = ctx;
        _ = id;
        _ = mode;
        const src: *align(1) const v.SVECTOR = @ptrCast(vec);
        sink.* = src.*;
    }

    pub fn func_8009DC8C(ctx: *zd.Ctx, id: anytype) void {
        // The id is the index func_800BFE50 returned, narrowed: below 24, never negative.
        const k: usize = @intCast(id);
        v.g.D_800F4B70()[k % 24] = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 16);
    buf[3] = ctx.pick(u8, &.{ 0x00, 0x01, 0x80, 0x81, 0xFF, 0xFE });
    buf[9] = ctx.pick(u8, &.{ 0xFF, 0xFE, 0x00, 0x01, 0x7F });
    if (ctx.chance(3)) buf[9] = 0xFF;
    v.g.D_800F4C2C().* = if (ctx.chance(2)) 2 else ctx.edge(u8, &.{ 0, 1, 3, 255 });
    sink = ctx.alloc(v.SVECTOR);
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const arg0 = args[0];
    if (v.g.D_800F4C2C().* == 2) return 3;
    if ((arg0[3] & 1) == 0) return 0;
    return if (arg0[9] == 0xFF) 1 else 2;
}
