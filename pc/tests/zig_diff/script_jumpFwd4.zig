//! zig_diff spec: vs_battle_script_jumpFwd4 -> script_jump_fwd4.script_jumpFwd4 (4A0A8.c).
//! Inputs: arg0 points at a fresh arena byte (the C only passes it to the
//! getShort stub, never reads it) and arg1 is an edge short. The stub returns
//! any 16-bit value, so the index is negative about half the time, and the
//! returned address is compared after the C's (long) to int narrowing. The C
//! has no branches; the classes split on arg1, which the C ignores in the
//! original but which a port could wrongly use.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_jumpFwd4));

pub const classes = [_][:0]const u8{ "arg1-zero", "arg1-nonzero" };

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const base: [*c]u8 = @ptrCast(ctx.allocBytes(1, 1));
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 0x7fff, -0x8000, 0x100 });
    return .{ base, arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[1] == 0) 0 else 1;
}
