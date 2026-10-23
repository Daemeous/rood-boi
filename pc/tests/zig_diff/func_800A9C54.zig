//! zig_diff spec: func_800A9C54 -> func_800A9C54.func_800A9C54 (40564.c).
//! Inputs: an arg0 that indexes the 17-entry D_800F4538 table (0..16, the slot
//! prepare sets to an arena D_800F4538_t with random bytes and a random or
//! edge unk5C2), an arg1 that is always a valid arena object (the C dereferences
//! it without a null test) with random bytes in its bitfield byte, and an arg2
//! that is -1 half the time and otherwise an edge or any value. The callee's
//! stub writes arg1's bytes, which the C reads back after the call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A9C54));

pub const classes = [_][:0]const u8{ "arg2-minus-one", "arg2-other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    obj.unk5C2 = ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFE, 0xFFFF });

    const tiles = v.g.D_800F4538();
    const arg0: c_int = ctx.range(c_int, 0, 16);
    tiles[@intCast(arg0)] = obj;

    // arg1 is dereferenced unconditionally: always a valid object (random bytes).
    const arg1 = ctx.alloc(v.func_8006EBF8_t_fields);

    const arg2: c_int = if (ctx.chance(2))
        -1
    else
        ctx.pick(c_int, &.{ 0, 1, 255, 256, -2, 0x7FFF_FFFF, -0x8000_0000 });

    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    return if (args[2] == -1) 0 else 1;
}

pub const stubs = struct {
    /// Void callee: it may rewrite the func_8006EBF8 block it was passed,
    /// which the C reads back (unk0_0, unk0_8, unk0_16) after the call.
    pub fn func_800AA290(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype, arg3: anytype) void {
        _ = arg0;
        _ = arg2;
        _ = arg3;
        const a: *v.func_8006EBF8_t_fields = @ptrCast(@alignCast(arg1));
        if (ctx.value(u1) == 1) a._bits_0[0] = ctx.value(u8);
        if (ctx.value(u1) == 1) a._bits_0[1] = ctx.value(u8);
        if (ctx.value(u1) == 1) a._bits_0[2] = ctx.value(u8);
    }
};
