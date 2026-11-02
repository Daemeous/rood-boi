//! zig_diff spec: func_800D2ADC -> func_800D2ADC.func_800D2ADC (5BF94.c).
//! Inputs: an effect (arg0, its frame unk14_16, bone matrices, the two aim
//! targets unkD1C.unkC/unk24 of kind 4 or a vector) whose event `event` (0..2)
//! is a definition in a D_800F569C block5 the spec builds: its flags pick the
//! origin (& 7), the shape (& 0x38: all eight), the shape's mode (& 0x1800),
//! the aim (& 0xE000), the pivot (& 0x1C0) and bit 0x40000; its curve bytes
//! index D_800F5330 (0..7); bones 0..15; ring sizes 0..5. arg2/arg3 zero or
//! not; parent null or a block. func_800CFE1C's stub answers by the curve it
//! is given: the frame divisor (1..3, so the frame test passes and fails), the
//! count (0..5, capped by D_800F55F0 - D_800F55F8 sometimes) or the shape's
//! sample; func_800D27F0 hands out arena nodes linked newest-first (its
//! counter lives in the arena, restored for the port's run); the lerp stubs
//! write small values (nonzero y: the cylinder divides by it). D_800EC328
//! (the ring's point) is 0..7 or any.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D2ADC));

pub const classes = [_][:0]const u8{ "skipped", "box", "octants", "cylinder", "disc", "flat", "ring4", "ring", "none" };

var def: *v.func_800FA098_arg0 = undefined;
var divisor: c_int = 1;
var count: c_int = 0;
var nodes: [6]*v.func_800D2904_t = undefined;
var taken: *u32 = undefined;

fn small(ctx: *zd.Ctx) c_int {
    return switch (ctx.value(u4)) {
        0, 1 => 0,
        2 => 46341, // squares past 2^31: RAND_UNIFORM_WIDE's other order
        3 => -50000,
        else => @rem(@as(c_int, ctx.value(i16)), 300),
    };
}

pub const stubs = struct {
    pub fn func_800CFE1C(ctx: *zd.Ctx, curve: anytype, t: anytype) c_int {
        _ = t;
        const p: usize = @intFromPtr(curve);
        if (p == @intFromPtr(&def.unkC0)) return divisor;
        if (p == @intFromPtr(&def.unkBC)) return count;
        // The shape's sample: +-1.0 makes the inner and outer bounds equal.
        return if (ctx.value(u2) == 0) ctx.edge(c_int, &.{ 4096, -4096, 0 }) else ctx.value(i16);
    }

    pub fn SquareRoot12(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return if (ctx.value(u2) == 0) ctx.edge(c_int, &.{ 4096, 4095, 4097 }) else ctx.value(u13);
    }

    pub fn func_800D27F0(ctx: *zd.Ctx, effect: anytype) ?*anyopaque {
        _ = ctx;
        _ = effect;
        const k = taken.*;
        taken.* += 1;
        return @ptrCast(nodes[@min(k, 5)]);
    }

    pub fn vs_battle_lerpVector(ctx: *zd.Ctx, src: anytype, t: anytype, out: anytype) void {
        _ = src;
        _ = t;
        const o: *v.VECTOR = @ptrCast(@alignCast(out));
        // The emitter's origin (scratch unk98) at 0 a quarter of the time.
        if (@intFromPtr(o) == 0x1F8001D0 + @offsetOf(v.func_800FA098_arg1, "unk98") and ctx.value(u2) == 0) {
            o.vx = 0;
            o.vy = 0;
            o.vz = 0;
            return;
        }
        o.vx = small(ctx);
        o.vy = small(ctx);
        // Never 0: the cylinder divides by it (SIGFPE in the original).
        if (o.vy == 0) o.vy = 7;
        o.vz = small(ctx);
    }

    pub fn vs_battle_lerpSvector(ctx: *zd.Ctx, src: anytype, t: anytype, out: anytype) void {
        _ = src;
        _ = t;
        const o: *v.SVECTOR = @ptrCast(@alignCast(out));
        o.vx = @truncate(small(ctx));
        o.vy = @truncate(small(ctx));
        o.vz = @truncate(small(ctx));
    }

    pub fn vs_battle_lerp2DVector(ctx: *zd.Ctx, src: anytype, t: anytype, out: anytype) void {
        _ = src;
        _ = t;
        out[0] = small(ctx);
        out[1] = small(ctx);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const effect = ctx.alloc(v.D_800F53B8_t);
    const header = ctx.alloc(v.D_800F569C_t);
    const block = ctx.allocBytes(4 + 3 * @sizeOf(v.func_800FA098_arg0), 16);
    psx.put(&header.block5Data, @as(*v.pFileBlock5, @ptrCast(@alignCast(block))));
    v.g.D_800F569C().* = header;
    const event = ctx.range(c_int, 0, 2);
    def = psx.at(&@as(*v.pFileBlock5, @ptrCast(@alignCast(block))).unk4, event);

    var flags: u32 = ctx.int(u32) & 0x3FF_FFFF;
    flags = (flags & ~@as(u32, 7)) | ctx.edge(u32, &.{ 0, 1, 2, 4, 3 });
    flags = (flags & ~@as(u32, 0xE000)) | ctx.edge(u32, &.{ 0, 0x2000, 0x4000, 0x6000, 0x8000, 0xA000 });
    flags = (flags & ~@as(u32, 0x1C0)) | ctx.edge(u32, &.{ 0, 0x40, 0x80, 0xC0, 0x140, 0x100 });
    psx.setBits(def, "flags", @truncate(flags));
    psx.setBits(def, "transparencyCurve", if (ctx.chance(2)) 0 else ctx.int(u6));
    def.unkC8 = ctx.range(u8, 0, 15);
    def.unkC9 = ctx.range(u8, 0, 15);
    def.unkCA = ctx.range(u8, 0, 5);
    def.unkCB = ctx.range(u8, 0, 5);
    inline for (.{ "unk8", "unk9", "unkA", "unkB", "unkC", "unkD", "unkE", "unkF", "unk10", "unk11", "unk12", "unk13", "unk14", "unk15", "unk16", "unk17" }) |f| {
        @field(def, f) = ctx.range(u8, 0, 7);
    }
    for (0..8) |k| v.g.D_800F5330()[k] = ctx.int(i16);

    psx.setBits(effect, "unk14_16", ctx.int(u16));
    effect.unkD1C.unkC.unk0 = ctx.edge(u16, &.{ 4, 0, 5 });
    effect.unkD1C.unk24.unk0 = ctx.edge(u16, &.{ 4, 0, 5 });

    divisor = ctx.edge(c_int, &.{ 1, 1, 1, 2, 3 });
    count = ctx.range(c_int, 0, 5);
    v.g.D_800F55F8().* = ctx.range(i32, 0, 20);
    v.g.D_800F55F0().* = v.g.D_800F55F8().* + ctx.range(i32, 1, 8);
    v.g.D_800EC328().* = if (ctx.chance(4)) ctx.int(i32) else ctx.range(i32, 0, 7);

    taken = ctx.alloc(u32);
    taken.* = 0;
    for (&nodes, 0..) |*n, k| {
        n.* = ctx.alloc(v.func_800D2904_t);
        if (k == 0) psx.put(&n.*.next, null) else psx.put(&n.*.next, nodes[k - 1]);
        // Near the origin (0 after >> 12) at times: the aim-away mode's zero vector.
        if (ctx.chance(2)) {
            n.*.unkC = ctx.edge(i32, &.{ 0, -1, -4095, 1 });
            n.*.unk10 = ctx.edge(i32, &.{ 0, -1, -4095, 1 });
            n.*.unk14 = ctx.edge(i32, &.{ 0, -1, -4095, 1 });
        }
    }

    const parent: ?*v.func_800FB4C0_t = if (ctx.chance(3)) null else ctx.alloc(v.func_800FB4C0_t);
    const arg2: c_int = if (ctx.chance(2)) 0 else ctx.int(c_int);
    const arg3: c_int = if (ctx.chance(2)) 0 else ctx.int(c_int);
    return .{ effect, event, arg2, arg3, parent };
}

pub fn classify(args: Args) usize {
    const frame: c_int = psx.getBits(args[0].?, "unk14_16");
    if (@rem(frame, divisor) != 0) return 0;
    return switch (psx.getBits(def, "flags") & 0x38) {
        0 => 1,
        8 => 2,
        16 => 3,
        24 => 4,
        32 => 5,
        40 => 6,
        48 => 7,
        else => 8,
    };
}
