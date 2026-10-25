//! zig_diff spec: func_8009DC38 -> func_8009DC38.func_8009DC38 (30DB0.c).
//! Inputs: an index 0..16 into the D_800F4538 pointer table. Every entry is
//! set by prepare: the chosen one is a null entry or a random arena object,
//! the others are null. func_8009DC8C is a stub that may clear the table
//! entry and may write the object's fields that the C overwrites after the
//! call (unk1A, unk18, and both halves of unk34), so a store moved across the
//! call, a store of the wrong width, or a re-read of the table shows up.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009DC38));

pub const classes = [_][:0]const u8{ "null-entry", "present-entry" };

/// The object prepare put in the table (null when the entry is null).
var obj: ?*v.D_800F4538_t = null;
/// The table index prepare chose.
var slot: usize = 0;

pub const stubs = struct {
    pub fn func_8009DC8C(ctx: *zd.Ctx, arg0: anytype) void {
        _ = arg0;
        // The callee may clear the table entry the C has already read.
        if (ctx.value(u2) == 0) v.g.D_800F4538()[slot] = null;
        const o = obj orelse return;
        // Fields the C overwrites after the call: a store moved before the
        // call is then left holding this value.
        if (ctx.value(u1) == 1) o.unk0.unk1A = ctx.value(u16);
        if (ctx.value(u1) == 1) o.unk0.unk18 = ctx.value(u16);
        if (ctx.value(u1) == 1) o.unk0.unk34.vx = ctx.value(i16);
        if (ctx.value(u1) == 1) o.unk0.unk34.vz = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    for (table) |*e| e.* = null;
    slot = ctx.range(usize, 0, 16);
    obj = null;
    if (!ctx.chance(4)) {
        const o = ctx.alloc(v.D_800F4538_t);
        table[slot] = o;
        obj = o;
    }
    return .{@as(c_int, @intCast(slot))};
}

pub fn classify(args: Args) usize {
    return if (v.g.D_800F4538()[@intCast(args[0])] == null) 0 else 1;
}
