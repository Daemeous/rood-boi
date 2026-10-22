//! zig_diff spec: func_800DA5CC -> func_800DA5CC.func_800DA5CC (71618.c).
//! Inputs: a da4bcState in the arena whose entries[0] points at an arena
//! da4bcEntry. Pending is zero about half the time and otherwise a
//! sign-bit or plain nonzero byte. target16 is a signed 8-bit field drawn
//! from edges (16 often, so `target < 16` sees equal sides) and random
//! values (negatives included). Mode and action come from edge sets (mode 1
//! and action 7 both occur). The callees are recording stubs: the two int
//! callees return zero, nonzero-with-sign-bit, plain nonzero and -1, and
//! they write the state fields and entry flags the C reads back.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_71618");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DA5CC));

pub const classes = [_][:0]const u8{ "pending", "target-ge16", "target-eq16", "target-negative", "target-small" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.da4bcState);
    const entry = ctx.alloc(v.da4bcEntry);
    psx.put(&state.entries[0], entry);
    state.pending = if (ctx.chance(2)) 0 else ctx.pick(u8, &.{ 1, 2, 0x80, 0xFF });
    state.mode = ctx.pick(u8, &.{ 0, 1, 2, 255 });
    state.action = ctx.pick(u8, &.{ 6, 7, 8, 0 });
    psx.setBits(state, "target16", ctx.pick(i8, &.{ 16, 15, 0, -1, -128, 127 }));
    return .{state};
}

pub fn classify(args: Args) usize {
    const state = args[0].?;
    if (state.pending != 0) return 0;
    const t = psx.getBits(state, "target16");
    if (t == 16) return 2;
    if (t > 16) return 1;
    if (t < 0) return 3;
    return 4;
}

/// Zero, 1, -1 (sign bit set) or a random value: each truth outcome with and
/// without the sign bit.
fn retval(ctx: *zd.Ctx) c_int {
    return switch (ctx.value(u8) % 4) {
        0 => 0,
        1 => 1,
        2 => -1,
        else => ctx.value(c_int),
    };
}

/// The da4bcState view of a C pointer or an optional Zig pointer.
fn view(p: anytype) *v.da4bcState {
    const q = if (@typeInfo(@TypeOf(p)) == .optional) p.? else p;
    return @ptrCast(@alignCast(q));
}

pub const stubs = struct {
    pub fn func_800DA4BC(ctx: *zd.Ctx, state: anytype, entry: anytype) c_int {
        const s = view(state);
        const e: *v.da4bcEntry = @ptrCast(@alignCast(entry));
        // The C reads entry->flagsC back for target15C.
        if (ctx.value(u1) == 1) e.flagsC = ctx.value(u32);
        // The C reads target16, mode and action back after a zero result too.
        if (ctx.value(u2) == 0) psx.setBits(s, "target16", ctx.value(i8));
        if (ctx.value(u2) == 0) s.mode = ctx.value(u8) % 3;
        return retval(ctx);
    }

    pub fn func_800DB370(ctx: *zd.Ctx, state: anytype, target: anytype, a: anytype, b: anytype) c_int {
        _ = target;
        _ = a;
        _ = b;
        const s = view(state);
        // The C reads mode and action back when this returns nonzero.
        if (ctx.value(u2) == 0) s.mode = ctx.value(u8) % 3;
        if (ctx.value(u2) == 0) s.action = ctx.value(u8) % 9;
        return retval(ctx);
    }

    pub fn func_800DB74C(ctx: *zd.Ctx, state: anytype, target: anytype) c_int {
        _ = state;
        _ = target;
        return retval(ctx);
    }

    pub fn func_800E678C(ctx: *zd.Ctx, p: anytype) void {
        if (ctx.value(u1) == 1) view(p).action = ctx.value(u8);
    }
};
