//! zig_diff spec: func_800E6178 -> init_actor_state.initActorState (7CD68.c).
//! Inputs: an actor whose attributes gate the init (attributes & 5 zero or
//! not, & 0x15 zero or not), id 0 or 1-15, kind 2/4 or not, a D_800F58BC
//! bank with 0-4 free slots, a model radius across the 63 and 128 edges,
//! stats fields across the bit-8 and range edges (field33 equal to field31
//! a third of the time), and options with the low short negative, bit 5
//! set, or clear, including the value 0 (the `>= 0` edge). The D_800F58B8
//! table has 256 rows so any tile index is in range. The callees that
//! write state (the allocator, the memset, the packed writer, the 5A9C
//! flags writer, the 7370 rangeSquared writer, 6828's distance, the
//! 800A190C result array, the 800A152C and DEC88 returns) are stubs that
//! write or return the values the C reads back, with rangeSquared equal to
//! temp*temp in a quarter of the calls.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_7CD68");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").initActorState));

pub const classes = [_][:0]const u8{ "early-return", "id-zero", "options-negative", "options-bit5", "options-plain" };

pub const stubs = struct {
    pub fn vs_main_allocHeap(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = size;
        return @ptrCast(ctx.allocBytes(0x484, 16));
    }

    pub fn func_800E4288(ctx: *zd.Ctx, p: anytype, n: anytype) void {
        _ = ctx;
        const bytes: [*]u8 = @ptrCast(@alignCast(p));
        const len: usize = @intCast(n);
        @memset(bytes[0..len], 0);
    }

    pub fn func_800A190C(ctx: *zd.Ctx, id: anytype, n: anytype, result: anytype, z: anytype) void {
        _ = id;
        _ = n;
        _ = z;
        const r: [*]u16 = @ptrCast(@alignCast(result));
        for (0..4) |i| r[i] = ctx.value(u16);
    }

    pub fn func_800A152C(ctx: *zd.Ctx, id: anytype, a: anytype, b: anytype) c_int {
        _ = id;
        _ = a;
        _ = b;
        if (ctx.value(u8) < 32) return 0;
        return ctx.value(c_int);
    }

    pub fn func_800E5A74(ctx: *zd.Ctx, p: anytype, q: anytype) void {
        _ = q;
        const pk: *c_int = @ptrCast(@alignCast(p));
        pk.* = ctx.value(c_int);
    }

    pub fn func_800E5A9C(ctx: *zd.Ctx, s: anytype, p: anytype) void {
        _ = p;
        const st: *v.actorInitializationState = @ptrCast(@alignCast(s));
        st.flags = ctx.value(u16);
    }

    pub fn func_800E6828(ctx: *zd.Ctx, s: anytype) void {
        const st: *v.actorInitializationState = @ptrCast(@alignCast(s));
        if (ctx.value(u1) == 1) st.distance = ctx.value(i32);
    }

    pub fn func_800E7370(ctx: *zd.Ctx, s: anytype) void {
        const st: *v.actorInitializationState = @ptrCast(@alignCast(s));
        const mode = ctx.value(u8);
        if (mode < 64) {
            // Equal to temp * temp, the C's `<` edge.
            const t = st.distance -% 64;
            st.rangeSquared = @bitCast(t *% t);
        } else if (mode < 128) {
            st.rangeSquared = ctx.value(u8);
        } else {
            st.rangeSquared = ctx.value(u32);
        }
    }

    pub fn func_800E72D0(ctx: *zd.Ctx) void {
        if (ctx.value(u1) == 1) v.g.D_800F590C().* = ctx.value(i32);
    }

    pub fn func_800DEC88(ctx: *zd.Ctx, p: anytype) c_int {
        _ = p;
        return if (ctx.value(u8) < 128) 1 else 0;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.actorInitializationActor);
    const model = ctx.alloc(v.actorInitializationModel);
    const stats = ctx.alloc(v.actorInitializationStats);

    // D_800F58BC: a bank whose int slots 10..13 (bytes 40..56) the C tests.
    const bank = ctx.allocBytes(64, 16);
    const ints: [*]i32 = @ptrCast(@alignCast(bank));
    if (ctx.chance(3)) ints[10 + ctx.range(usize, 0, 3)] = 0;
    const bk: *v.D_800F58BC_t = @ptrCast(@alignCast(bank));
    v.g.D_800F58BC().* = bk;

    // D_800F58B8: 256 rows of 32 u16, plus slack for the largest index.
    const table = ctx.allocBytes(256 * 64 + 512, 16);
    const t: [*][32]u16 = @ptrCast(@alignCast(table));
    v.g.D_800F58B8().* = t;

    actor.id = if (ctx.chance(8)) 0 else ctx.range(i32, 1, 15);
    actor.attributes = ctx.pick(u16, &.{ 0, 0x15, 0x10, 0x5, 0xFFEA, 0x8000 });
    actor.field1F = ctx.pick(u8, &.{ 0, 1, 2 });
    actor.field30 = ctx.int(i32);
    actor.field34 = ctx.int(u32);
    actor.field38 = ctx.int(i32);
    actor.field40 = if (ctx.chance(2)) 0 else ctx.int(i32);
    psx.put(&actor.stats, stats);
    psx.put(&actor.model, model);

    stats.field30 = ctx.pick(u8, &.{ 0, 8, 0x10, 0x18, 0xFF, 0x80 });
    stats.field31 = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    stats.field32 = ctx.pick(u8, &.{ 0, 8, 0x10, 0x88 });
    stats.field33 = if (ctx.chance(3)) stats.field31 else ctx.pick(u8, &.{ 0, 1, 255 });

    model.flags = ctx.int(u32);
    model.kind = if (ctx.chance(2)) ctx.range(u32, 0, 7) else ctx.int(u32);
    model.radius = ctx.pick(u16, &.{ 0, 40, 62, 63, 64, 127, 128, 129, 0xFFFF });
    model.position = .{ ctx.int(i16), ctx.pick(i16, &.{ 0, -1, 32767, -32768 }), ctx.int(i16) };
    model.field63E = ctx.int(u16);

    const options = ctx.pick(c_int, &.{ 0, 0x20, 0x40, 0x8000, -32768, 0x10000, -1 });
    return .{ actor, options };
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    if ((actor.attributes & 5) == 0) return 0;
    if (actor.id == 0) return 1;
    const low: i16 = @truncate(args[1]);
    if (low < 0) return 2;
    if ((args[1] & 0x20) != 0) return 3;
    return 4;
}
