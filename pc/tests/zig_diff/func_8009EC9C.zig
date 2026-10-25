//! zig_diff spec: func_8009EC9C -> func_8009EC9C.func_8009EC9C (30DB0.c).
//! Inputs: an actor entry of D_800F4538 (index 0-16, sometimes null) with
//! edge positions and facings (0, +-2048, 2049, 4096...), a target SVECTOR
//! (sometimes null, sometimes on the actor's x or z so dx or dz is 0,
//! sometimes on both), coordinates that overflow dist*dist, an anim index
//! 0-13 into D_800E8E80 (unsigned bytes, so the high half is seen), and arg3
//! of 0, 1, 2 or -1. The ratan2 stub returns the pitch boundary values
//! (-228, -229, 172, 171), the angle wrap bounds (1024, 3072), 0, or any
//! value, so every ordered compare on the pitch and the angle sees both
//! sides equal many times.
//! func_800A1AF8 fills its out-parameter (the C reads sp10.vy back).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009EC9C));

pub const classes = [_][:0]const u8{ "no-actor", "no-target", "same-spot", "one-axis-equal", "aimed" };

pub const stubs = struct {
    /// Returns the values the C compares: -228 and -229 for the lower pitch
    /// bound, 172 and 171 for the upper one, 1024 and 3072 for the angle wrap
    /// bounds, 0, or any value. Both sides of each compare are equal often.
    pub fn ratan2(ctx: *zd.Ctx, y: anytype, x: anytype) c_int {
        _ = y;
        _ = x;
        const sel = ctx.value(u8);
        return switch (sel) {
            0...31 => -228,
            32...47 => -229,
            48...79 => 172,
            80...111 => 171,
            112...127 => 1024,
            128...143 => 3072,
            144...159 => 0,
            else => ctx.value(c_int),
        };
    }

    /// Writes the out-parameter the C reads back (sp10.vy); the other
    /// components are written too, as the real callee fills the whole vector.
    pub fn func_800A1AF8(ctx: *zd.Ctx, a: anytype, b: anytype, out: anytype, d: anytype) void {
        _ = a;
        _ = b;
        _ = d;
        out.*.vx = ctx.value(i16);
        out.*.vy = ctx.value(i16);
        out.*.vz = ctx.value(i16);
        out.*.pad = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const idx: u5 = ctx.range(u5, 0, 16);
    v.g.D_800F4538()[idx] = if (ctx.chance(8)) null else actor;

    const pos = &actor.unk0.position;
    pos.vx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    pos.vy = ctx.int(i16);
    pos.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    actor.unk0.facing = ctx.pick(i16, &.{ 0, 0, 2048, 2048, -2048, 1, -1, 2049, -2049, 4095, 4096 });

    // Small values keep anim += D_800F49D8 from overflowing (C UB).
    v.g.D_800F49D8().* = ctx.range(i32, -4, 4);

    var target: ?*v.SVECTOR = null;
    if (!ctx.chance(5)) {
        const s = ctx.alloc(v.SVECTOR);
        s.vx = if (ctx.chance(3)) pos.vx else ctx.int(i16);
        s.vy = ctx.int(i16);
        s.vz = if (ctx.chance(3)) pos.vz else ctx.int(i16);
        target = s;
    }

    const arg2: u8 = ctx.range(u8, 0, 13);
    const arg3 = ctx.pick(c_int, &.{ 0, 1, 2, -1 });
    return .{ @as(c_int, idx), target, @as(c_int, arg2), arg3 };
}

pub fn classify(args: Args) usize {
    const actor = v.g.D_800F4538()[@intCast(args[0])] orelse return 0;
    const t = args[1] orelse return 1;
    const dx = @as(i32, t.vx) - @as(i32, actor.unk0.position.vx);
    const dz = @as(i32, t.vz) - @as(i32, actor.unk0.position.vz);
    if (dx == 0 and dz == 0) return 2;
    if (dx == 0 or dz == 0) return 3;
    return 4;
}
