//! zig_diff spec: vs_battle_script_jumpFwd3 -> script_jump_fwd3.script_jumpFwd3 (4A0A8.c).
//! Inputs: a script pointer either into the arena or into PS1 RAM at address 0.
//! The C only forms addresses and never reads through them; the callee stub
//! returns any short, so the offset is negative about half the time and the
//! pointer walks backwards. The result truncates the address to an int, so
//! both the arena and low-RAM addresses exercise that truncation.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_jumpFwd3));

pub const classes = [_][:0]const u8{ "arena", "low-ram" };

var in_low: bool = false;

pub const stubs = struct {
    /// The script offset: any short, drawn only through ctx.value.
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    in_low = ctx.chance(4);
    const p: [*c]u8 = if (in_low)
        @ptrCast(psx.lowRam(u8))
    else
        @ptrCast(ctx.allocBytes(16, 1));
    return .{ p, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (in_low) 1 else 0;
}
