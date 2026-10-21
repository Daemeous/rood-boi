//! zig_diff spec: func_800A1720 -> func_800A1720.func_800A1720 (38C1C.c).
//! Inputs: arg0 is a slot of D_800F4538 (0..16), each slot either null or the
//! one D_800F4538_t object in the arena. arg1 is 0xFD about half the time, else
//! an edge value (0, 0xFC, 0xFE, -3, 0x7F, -1). arg2 is an arena buffer of
//! eight ints; arg3 is null or another arena buffer. The callee
//! func_800A17BC is a recording stub returning 0, -1, 1, or a random int
//! (deterministic per call), so both sides of `var_v0 < 0` see equality and
//! both signs. Classes: arg1 not 0xFD, arg1 0xFD with a null slot, and arg1
//! 0xFD with a live slot.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_38C1C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A1720));

pub const classes = [_][:0]const u8{ "not-fd", "fd-null-slot", "fd-live-slot" };

pub const stubs = struct {
    pub fn func_800A17BC(ctx: *zd.Ctx, arg0: c_int, arg1: c_int, arg2: anytype, arg3: anytype) c_int {
        _ = arg0;
        _ = arg1;
        _ = arg2;
        _ = arg3;
        const r = ctx.value(u32);
        return switch (r % 4) {
            0 => 0,
            1 => -1,
            2 => 1,
            else => @as(c_int, @bitCast(r)),
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    const buf = ctx.alloc([8]c_int);
    const other = ctx.alloc([4]c_int);

    const table = v.g.D_800F4538();
    for (table) |*slot| {
        slot.* = if (ctx.chance(2)) null else obj;
    }

    const arg0: c_int = ctx.range(c_int, 0, 16);
    const arg1: c_int = if (ctx.chance(2))
        0xFD
    else
        ctx.pick(c_int, &.{ 0, 0xFC, 0xFE, -3, 0x7F, -1 });
    const arg2: [*c]c_int = &buf[0];
    const arg3: [*c]c_int = if (ctx.chance(2)) null else &other[0];
    return .{ arg0, arg1, arg2, arg3 };
}

pub fn classify(args: Args) usize {
    if (args[1] != 0xFD) return 0;
    const table = v.g.D_800F4538();
    return if (table[@intCast(args[0])] == null) 1 else 2;
}
