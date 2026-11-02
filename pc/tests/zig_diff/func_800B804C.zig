//! zig_diff spec: func_800B804C -> func_800B804C.func_800B804C (4A0A8.c).
//! The C has no branches: it reads three shorts at arg0+3, +5, +7 into a
//! local SVECTOR, calls func_800BFE50 on the short at arg0+1, passes that
//! result, the vector and the byte at arg0[9] to func_800AABD0, and returns 0.
//! Inputs: a 16-byte block in the arena filled with random bytes, so every
//! short and the trailing byte vary (signed and unsigned reads, negative
//! values). arg1 is random and unused by the C. vs_battle_getShort returns
//! any 16-bit value through ctx.value; the callee stubs for func_800BFE50 and
//! func_800AABD0 keep the harness defaults (a null pointer, a recorded call).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B804C));

pub const classes = [_][:0]const u8{"call"};

pub const stubs = struct {
    /// Reads the game's memory at its argument in the real callee; here the
    /// result is a fresh 16-bit value, the same in both runs.
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    // 16 bytes the C reads at offsets 1, 3, 5, 7 and 9; random from the arena fill.
    const block: [*]u8 = ctx.allocBytes(16, 1);
    return .{ block, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
