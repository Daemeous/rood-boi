//! zig_diff spec: func_800A9D24 -> func_800A9D24.func_800A9D24 (40564.c).
//! Inputs: an index 0..16 into D_800F4538 with an arena D_800F4538_t in that
//! slot (or null, for the arg2 == -1 path, which never reads it), an optional
//! SVECTOR, and an arg2 that is -1 or an edge value (0, 1, -2, 0x7FFF, 0xFFFF,
//! 0x10000 which truncates to 0 in the u16 field, -65535, INT_MIN) or any int.
//! The callee func_800AA698 is a recording stub that may rewrite the SVECTOR
//! and the actor's unk5C2 field, which the C restores after the call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A9D24));

pub const classes = [_][:0]const u8{ "minus-one", "minus-one-null-actor", "store" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const arg0 = ctx.range(c_int, 0, 16);
    const arg2: c_int = if (ctx.chance(4))
        -1
    else
        ctx.pick(c_int, &.{ 0, 1, -2, 0x7FFF, 0xFFFF, 0x10000, 0x12345, -65535, std.math.minInt(c_int) });

    const actor = ctx.alloc(v.D_800F4538_t);
    if (arg2 == -1 and ctx.chance(2)) {
        table[@intCast(arg0)] = null;
    } else {
        table[@intCast(arg0)] = actor;
    }

    const sv: ?*v.SVECTOR = if (ctx.chance(5)) null else ctx.alloc(v.SVECTOR);
    return .{ arg0, sv, arg2 };
}

pub fn classify(args: Args) usize {
    if (args[2] != -1) return 2;
    const table = v.g.D_800F4538();
    return if (table[@intCast(args[0])] == null) 1 else 0;
}

pub const stubs = struct {
    pub fn func_800AA698(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype) void {
        _ = arg2;
        const p: ?*v.SVECTOR = @ptrCast(arg1);
        if (p) |sv| {
            if (ctx.value(u1) == 1) sv.vx = ctx.value(i16);
        }
        if (ctx.value(u1) == 1) {
            const table = v.g.D_800F4538();
            const a: ?*v.D_800F4538_t = table[@intCast(arg0)];
            if (a) |actor| actor.unk5C2 = ctx.value(u16);
        }
    }
};
