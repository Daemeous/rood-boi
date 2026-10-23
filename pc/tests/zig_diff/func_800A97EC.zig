//! zig_diff spec: func_800A97EC -> func_800A97EC.func_800A97EC (40564.c).
//! Inputs: a slot index 0..16 whose D_800F4538 entry is an arena actor or null,
//! a 4-byte func_8006EBF8_t_fields with random bit fields (unk0_24 is often 0xFF),
//! and arg2/arg3 from edges (0, -1, 1, 65535, INT_MAX, INT_MIN) or any int. The
//! callees are recording stubs that write the bytes the C reads back after them
//! (the arg1 fields, the actor's facing) and return ctx.value for the int result.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A97EC));

pub const classes = [_][:0]const u8{ "no-actor", "reset-tile", "else-arg2-zero", "else-arg3-zero", "else-both-set" };

var cur_actor: ?*v.D_800F4538_t = null;
var cur_arg1: ?*v.func_8006EBF8_t_fields = null;

pub const stubs = struct {
    pub fn func_800AA290(ctx: *zd.Ctx, slot: anytype, arg1: anytype, a: anytype, b: anytype) void {
        _ = slot;
        _ = arg1;
        _ = a;
        _ = b;
        // The C reads arg1's unk0_24 and the actor's facing after this call.
        if (ctx.value(u1) == 1) psx.setBits(cur_arg1.?, "unk0_24", ctx.value(u8));
        if (ctx.value(u1) == 1) cur_actor.?.unk0.facing = ctx.value(i16);
    }

    pub fn func_800AA984(ctx: *zd.Ctx, slot: anytype, angle: anytype, b: anytype) void {
        _ = slot;
        _ = angle;
        _ = b;
        // The C reads arg1's unk0_0 after this call.
        if (ctx.value(u1) == 1) psx.setBits(cur_arg1.?, "unk0_0", ctx.value(u8));
    }

    pub fn func_800AA490(ctx: *zd.Ctx, slot: anytype, arg1: anytype, a: anytype, b: anytype) void {
        _ = slot;
        _ = arg1;
        _ = a;
        _ = b;
        // The C reads arg1's unk0_0 and unk0_16 after this call.
        if (ctx.value(u1) == 1) psx.setBits(cur_arg1.?, "unk0_16", ctx.value(u8));
    }

    pub fn func_800A6EE8(ctx: *zd.Ctx, vec: anytype, a: anytype, b: anytype, c2: anytype) c_int {
        _ = vec;
        _ = a;
        _ = b;
        _ = c2;
        // The C reads arg1's unk0_24 (the pad test) after this call.
        if (ctx.value(u1) == 1) psx.setBits(cur_arg1.?, "unk0_24", ctx.value(u8));
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const slot = ctx.range(c_int, 0, 16);
    const actor = ctx.alloc(v.D_800F4538_t);
    actor.unk0.facing = ctx.pick(i16, &.{ 0, 1, -1, 4080, 32767, -32768 });
    actor.unk5C0 = ctx.int(u16);
    actor.unk5C2 = ctx.int(u16);
    v.g.D_800F4538()[@intCast(slot)] = if (ctx.chance(5)) null else actor;

    const f = ctx.alloc(v.func_8006EBF8_t_fields);
    psx.setBits(f, "unk0_0", ctx.int(u8));
    psx.setBits(f, "unk0_8", ctx.int(u8));
    psx.setBits(f, "unk0_16", ctx.int(u8));
    psx.setBits(f, "unk0_24", ctx.pick(u8, &.{ 0xFF, 0xFF, 0, 1, 0x80, 0xFE, 0x0F }));

    const arg2 = ctx.pick(c_int, &.{ 0, 0, -1, 1, 65535, 0x7FFF_FFFF, std.math.minInt(c_int) });
    const arg3 = ctx.pick(c_int, &.{ 0, 0, -1, 1, 65535, 0x7FFF_FFFF, std.math.minInt(c_int) });

    cur_actor = actor;
    cur_arg1 = f;
    return .{ slot, f, arg2, arg3 };
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F4538();
    if (table[@intCast(args[0])] == null) return 0;
    if (args[2] == 0 and args[3] == 0) return 1;
    if (args[2] == 0) return 2;
    if (args[3] == 0) return 3;
    return 4;
}
