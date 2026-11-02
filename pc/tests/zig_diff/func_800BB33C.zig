//! zig_diff spec: func_800BB33C -> func_800BB33C.func_800BB33C (4A0A8.c).
//! The C body makes one void call and returns 0, with no branches and no
//! value tests. The inputs are a random `u_char*` (null sometimes, though
//! the C never reads it) and a random `short`, which the C ignores. The
//! callee `func_800BE3A0` is in the battle overlay and may change any state
//! the C can reach, so its stub writes a random word into the scratchpad.
//! Both runs see the same write, so the state comparison and the call trace
//! catch a dropped or doubled call, and the return-value mutations are caught
//! by the return comparison. Classes split the inputs by null pointer and by
//! the sign of the short, so every combination is counted.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB33C));

pub const classes = [_][:0]const u8{
    "non-null-nonneg",
    "non-null-neg",
    "null-nonneg",
    "null-neg",
};

pub const stubs = struct {
    pub fn func_800BE3A0(ctx: *zd.Ctx) void {
        // Models a write the callee can make: one scratchpad word.
        const word: *align(1) u32 = @ptrFromInt(zd.scratchpad + 0x40);
        word.* = ctx.value(u32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const p: [*c]u8 = if (ctx.chance(2)) null else @ptrCast(ctx.allocBytes(1, 1));
    const n: c_short = ctx.pick(c_short, &.{ 0, -1, 1, 32767, -32768 });
    return .{ p, n };
}

pub fn classify(args: Args) usize {
    const nul = args[0] == null;
    const neg = args[1] < 0;
    const base: usize = if (nul) 2 else 0;
    return base + (if (neg) @as(usize, 1) else 0);
}
