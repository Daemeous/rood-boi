//! zig_diff spec: func_80094B0C -> func_80094B0C.func_80094B0C (2842C.c).
//! Inputs: a 512-byte D_800F1BAC_t in the arena with unkB at 0/15/16/17/255
//! (the `< 16` edge), unkC zero about a quarter of the time (so the
//! `unkC & 0x8000` test sees its operand equal to 0 often), 0x8000 and 0xFFFF
//! sometimes and random otherwise (sign bit both ways), unkE below, at and
//! above 3, and displayTextLen 0-255 (the loop reads displayText up to byte
//! 274). x is either in the on-screen band around -8..319 or an edge (320 for
//! the `x + 8 == 328` equality, -9 where the unsigned test wraps, the int
//! extremes), and y runs over the wrap edges. The scratchpad's unk0 and unk4
//! point at arena blocks for the prims and the ordering table. func_80094E18
//! rewrites the text fields the loop reads and returns a small offset; AddPrim
//! is a recording void stub.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80094B0C));

pub const classes = [_][:0]const u8{ "callee-path", "low-sign", "low-plain" };

pub const stubs = struct {
    pub fn func_80094E18(ctx: *zd.Ctx, x: anytype, y: anytype, p: anytype) c_int {
        _ = x;
        _ = y;
        const q: *v.D_800F1BAC_t = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) q.displayTextLen = ctx.value(u8);
        if (ctx.value(u1) == 1) q.displayText[0] = ctx.value(u8);
        return @as(c_int, ctx.value(i16));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const scr: *v.vs_scratch_t = @ptrFromInt(zd.scratchpad);
    const prims: *anyopaque = @ptrCast(ctx.allocBytes(4096, 16));
    const ot: *anyopaque = @ptrCast(ctx.allocBytes(64, 16));
    psx.put(&scr.unk0, prims);
    psx.put(&scr.unk4, ot);

    const p: *v.D_800F1BAC_t = @ptrCast(@alignCast(ctx.allocBytes(512, 16)));
    p.unkB = ctx.pick(u8, &.{ 0, 15, 16, 17, 255 });
    // The flag-test operand (unkC) equal to 0 in about a quarter of the runs,
    // 0x8000 and 0xFFFF sometimes, random otherwise.
    p.unkC = switch (ctx.range(u8, 0, 7)) {
        0, 1 => 0,
        2 => 0x8000,
        3 => 0xFFFF,
        else => ctx.int(u16),
    };
    p.unkE = ctx.pick(u8, &.{ 0, 1, 2, 3, 4, 5, 255 });
    p.displayTextLen = ctx.pick(u8, &.{ 0, 1, 2, 3, 4, 5, 255 });

    const x: c_int = if (ctx.chance(2))
        ctx.range(c_int, -8, 319)
    else
        ctx.pick(c_int, &.{ 320, 319, 321, -8, -9, 0, 100, -1, std.math.minInt(c_int), std.math.maxInt(c_int) });
    const y: c_int = ctx.pick(c_int, &.{ 0, 100, -1, 120, std.math.minInt(c_int), std.math.maxInt(c_int) });
    return .{ x, y, p };
}

pub fn classify(args: Args) usize {
    const p = args[2].?;
    if (p.unkE >= 3) return 0;
    if ((p.unkC & 0x8000) != 0) return 1;
    return 2;
}
