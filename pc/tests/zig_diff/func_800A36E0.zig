//! zig_diff spec: func_800A36E0 -> func_800A36E0.func_800A36E0 (3A1A0.c).
//! Inputs: an index 0-16 into D_800F4538 (every entry is a shared random
//! block or null; the selected one is an actor or null), the actor's facing
//! at the /16 and sign edges, its unkA_5 bit, and arg1 as 0x10, 0x20 or
//! another value. arg2 is always a valid random output object. Every entry
//! of the table is a valid object or null, so an index into the table never
//! reads outside it. The C makes no calls, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A36E0));

pub const classes = [_][:0]const u8{ "no-actor", "flag-clear", "flag-set-0x10", "flag-set-0x20", "flag-set-other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const other = ctx.alloc(v.D_800F4538_t);
    const actor = ctx.alloc(v.D_800F4538_t);
    const out = ctx.alloc(v.func_8006EBF8_t);

    actor.unk0.facing = ctx.pick(i16, &.{ 0, 1, -1, 15, 16, -15, -16, -17, 32767, -32768 });
    psx.setBits(&actor.unk0, "unkA_5", @intFromBool(ctx.chance(2)));

    // Every entry is a valid object or null, so any index in 0..16 is safe to read.
    for (table.*[0..]) |*slot| slot.* = if (ctx.chance(3)) null else other;

    const idx = ctx.range(u8, 0, 16);
    table.*[idx] = if (ctx.chance(4)) null else actor;

    const arg1: c_int = switch (ctx.range(u8, 0, 3)) {
        0 => 0x10,
        1 => 0x20,
        2 => 0,
        else => ctx.int(c_int),
    };
    return .{ @as(c_int, idx), arg1, out };
}

pub fn classify(args: Args) usize {
    const actor = v.g.D_800F4538().*[@intCast(args[0])] orelse return 0;
    if (psx.getBits(&actor.unk0, "unkA_5") == 0) return 1;
    return switch (args[1]) {
        0x10 => 2,
        0x20 => 3,
        else => 4,
    };
}
