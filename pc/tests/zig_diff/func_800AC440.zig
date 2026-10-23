//! zig_diff spec: func_800AC440 -> func_800AC440.func_800AC440 (40564.c).
//! Inputs: a D_800F4538_t in the arena whose unk68 is a 4096-byte armature
//! block, so every armatures[arg1] the C addresses (arg1 in -5..256) stays
//! owned. arg1 takes its edges (negative, 0..3, 63, 255, 256: 256 never
//! equals a u8 slot). The slot pair unk18D0 holds 0, arg1's byte, or a random
//! byte, so each release match, each claim slot and the full pair occur. arg2
//! is 0 half the time, otherwise a nonzero edge (-1 and INT_MIN carry the sign bit).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AC440));

pub const classes = [_][:0]const u8{
    "release-match-0",
    "release-match-1",
    "release-none",
    "claim-slot-0",
    "claim-slot-1",
    "claim-full",
};

pub const stubs = struct {
    /// The callee fills its out-parameter; the C never reads it back, but the
    /// real callee may write it, so the stub does too.
    pub fn func_800A1720(ctx: *zd.Ctx, slot: anytype, id: anytype, data: anytype, out: anytype) void {
        _ = slot;
        _ = id;
        _ = data;
        const p: *c_int = @ptrCast(@alignCast(out));
        p.* = ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.alloc(v.D_800F4538_t);
    const block: *v.D_800F4538_unk68 = @ptrCast(@alignCast(ctx.allocBytes(4096, 16)));
    psx.put(&arg0.unk0.unk68, block);
    arg0.unk0.unkF = ctx.int(u8);

    const arg1: c_int = ctx.edge(c_int, &.{ -5, -1, 0, 1, 2, 3, 63, 255, 256 });
    const arg2: c_int = if (ctx.chance(2)) 0 else ctx.pick(c_int, &.{ 1, -1, 2, -2147483648 });

    const match: u8 = @truncate(@as(c_uint, @bitCast(arg1)));
    var i: usize = 0;
    while (i < 2) : (i += 1) {
        arg0.unk18D0[i] = switch (ctx.range(u8, 0, 3)) {
            0 => 0,
            1 => match,
            else => ctx.int(u8),
        };
    }
    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const d = args[0].?;
    const arg1 = args[1];
    const arg2 = args[2];
    if (arg2 == 0) {
        for (0..2) |i| if (@as(c_int, d.unk18D0[i]) == arg1) return i;
        return 2;
    }
    if (d.unk18D0[0] == 0) return 3;
    if (d.unk18D0[1] == 0) return 4;
    return 5;
}
