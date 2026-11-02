//! zig_diff spec: func_800B792C -> func_800B792C.func_800B792C (4A0A8.c).
//! Inputs: a 16-byte packet in the arena. Bytes 1-8 hold four shorts (edge
//! values often, random otherwise). Byte 3 is drawn from edges with and
//! without bit 0 and the sign bit. Bytes 9-11 are 0xFF about a third of the
//! time, which reaches both arms of each ternary. D_800F4C2C is 2 about half
//! the time, so both sides of the hook test run. The stubs read the shorts as
//! a little-endian getShort does, return a table index below 24 from
//! func_800BFE50, fill the out-vector of func_800A1108 with random shorts, and
//! copy the vector func_800A9988 receives into an arena block, so the stored
//! vector, pad and applyVector sums are all observed.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B792C));

pub const classes = [_][:0]const u8{ "add-hook2", "add-hook-other", "set-hook2", "set-hook-other" };

/// The vector func_800A9988 was handed, copied into the arena each iteration.
var sink: *v.SVECTOR = undefined;

fn asVec(p: anytype) *const v.SVECTOR {
    if (@typeInfo(@TypeOf(p)) == .optional) return @ptrCast(@alignCast(p.?));
    return @ptrCast(@alignCast(p));
}

pub const stubs = struct {
    /// Reads the little-endian short at the pointer, as the real getShort does.
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = ctx;
        const b: [*]const u8 = @ptrCast(p);
        const u: u16 = @as(u16, b[0]) | (@as(u16, b[1]) << 8);
        return @bitCast(u);
    }

    /// A table index below 24 (the D_800F4B70 table has 24 entries).
    pub fn func_800BFE50(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        return @as(c_int, ctx.value(u8) % 24);
    }

    /// Writes the out-vector the C then adds to (applyVector) or overwrites.
    pub fn func_800A1108(ctx: *zd.Ctx, id: anytype, out: ?*anyopaque) c_int {
        _ = id;
        const p: *v.func_8006EBF8_t = @ptrCast(@alignCast(out.?));
        p.unk0.unk4.vx = ctx.value(i16);
        p.unk0.unk4.vy = ctx.value(i16);
        p.unk0.unk4.vz = ctx.value(i16);
        return ctx.value(c_int);
    }

    /// Records the vector it is given into the arena block `sink`.
    pub fn func_800A9988(ctx: *zd.Ctx, id: anytype, vec: anytype, a: anytype, b: anytype) void {
        _ = ctx;
        _ = id;
        _ = a;
        _ = b;
        sink.* = asVec(vec).*;
    }

    pub fn func_8009DC8C(ctx: *zd.Ctx, id: anytype) void {
        _ = ctx;
        _ = id;
    }
};

fn putShort(buf: [*]u8, off: usize, s: i16) void {
    const u: u16 = @bitCast(s);
    buf[off] = @truncate(u);
    buf[off + 1] = @truncate(u >> 8);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 16);
    sink = ctx.alloc(v.SVECTOR);

    putShort(buf, 1, ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 255, 256 }));
    putShort(buf, 3, ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 255, 256 }));
    putShort(buf, 5, ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 255, 256 }));
    putShort(buf, 7, ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 255, 256 }));
    // Byte 3 is also the low byte of the second short: set it last so the
    // flag test sees edges with and without bit 0 and the sign bit.
    buf[3] = ctx.pick(u8, &.{ 0, 1, 0x80, 0x81, 0xFF });

    buf[9] = if (ctx.chance(3)) 0xFF else ctx.int(u8);
    buf[10] = if (ctx.chance(3)) 0xFF else ctx.int(u8);
    buf[11] = if (ctx.chance(3)) 0xFF else ctx.int(u8);

    v.g.D_800F4C2C().* = if (ctx.chance(2)) 2 else ctx.pick(u8, &.{ 0, 1, 3, 255 });

    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const add = args[0][3] & 1 != 0;
    const hook2 = v.g.D_800F4C2C().* == 2;
    return (if (add) @as(usize, 0) else 2) + (if (hook2) @as(usize, 0) else 1);
}
