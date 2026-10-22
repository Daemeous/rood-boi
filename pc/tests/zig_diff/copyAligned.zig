//! zig_diff spec: vs_battle_copyAligned -> copy_aligned.copyAligned (573B8.c).
//! Inputs: two 64-byte arena buffers with random contents (the copied shorts
//! carry both signs) and an even numBytes, since an odd one never reaches a
//! multiple of 8 and the C loops forever. numBytes covers the aligned sizes
//! (no loop: zero, positive and negative multiples of 8), and the loop lengths
//! 1, 2 and 3 (numBytes = 2, 4, 6 mod 8) both positive and negative. Aligned
//! sizes may pass null pointers, which the C never dereferences. The C reads
//! nothing back from vs_battle_memcpy, so that callee needs no stub: the call
//! and its arguments are recorded.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").copyAligned));

pub const classes = [_][:0]const u8{ "aligned", "aligned-negative", "loop-1", "loop-2", "loop-3", "loop-negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const dst: *anyopaque = @ptrCast(ctx.allocBytes(64, 16));
    const src: *const anyopaque = @ptrCast(ctx.allocBytes(64, 16));
    var n = ctx.pick(c_int, &.{ 0, 2, 4, 6, 8, -2, -4, -6, -8, 14, -14, 0x7FFF_FFFE, -0x8000_0000, 0x4000_0000 });
    // Even only: an odd numBytes never terminates the C loop.
    n &= ~@as(c_int, 1);

    var d: ?*anyopaque = dst;
    var s: ?*const anyopaque = src;
    // Aligned sizes never touch the pointers: null is legal there.
    if ((n & 7) == 0 and ctx.chance(5)) {
        if (ctx.chance(2)) d = null else s = null;
    }
    return .{ d, s, n };
}

pub fn classify(args: Args) usize {
    const n = args[2];
    const low = n & 7;
    if (n < 0) return if (low == 0) 1 else 5;
    if (low == 0) return 0;
    return if (low == 2) 2 else if (low == 4) 3 else 4;
}
