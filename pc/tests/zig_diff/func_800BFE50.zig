//! zig_diff spec: func_800BFE50 -> func_800BFE50.func_800BFE50 (573B8.c).
//! Inputs: arg0 with its top nibble 0, 1 or 2 (the three cases the C
//! switches on) and random low bits, with edge low parts (0, 0x1F, 0x20,
//! 0x800, 0xFFF) half the time. Case 0 forwards arg0 & 0xFFF to
//! func_8007CF18, whose stub returns any c_int; case 1 returns arg0 & 0x1F;
//! case 2 returns arg0 itself. Nibbles 3-15 make the C fall off the end
//! with no return (undefined behaviour), so they are not generated: the
//! switch's default edge is waived in specs.toml.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_573B8");
const c = @import("c_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BFE50));

pub const classes = [_][:0]const u8{ "call-case-0", "mask-case-1", "identity-case-2" };

pub const stubs = struct {
    pub fn func_8007CF18(ctx: *zd.Ctx, arg: anytype) c_int {
        _ = arg;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const low: u16 = ctx.pick(u16, &.{ 0, 0x1F, 0x20, 0x800, 0xFFF });
    const sel: u16 = ctx.range(u16, 0, 2);
    const x: u16 = (sel << 12) | (low & 0xFFF);
    return .{x};
}

pub fn classify(args: Args) usize {
    return @as(usize, args[0] >> 12);
}
