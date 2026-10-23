//! zig_diff spec: func_800AAC80 -> func_800AAC80.func_800AAC80 (40564.c).
//! Inputs: a DR_MOVE in the arena whose guest address sits in scratchpad slot 0
//! (the C reads that word as its pointer), a RECT or null, and x and y drawn from
//! 0, +-1, 320, 512 and the int extremes. SetDrawMove and func_8007A824 are
//! recording stubs that write the DR_MOVE the way the real ones do (its tag and
//! code words, the ordering-table link), so a port that reads the object back
//! before or after the calls diverges. The C has no branches; the classes split
//! the null and non-null RECT argument.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AAC80));

pub const classes = [_][:0]const u8{ "rect-null", "rect-valid" };

pub const stubs = struct {
    pub fn SetDrawMove(ctx: *zd.Ctx, p: anytype, rect: anytype, x: anytype, y: anytype) void {
        _ = rect;
        _ = x;
        _ = y;
        const dm: *v.DR_MOVE = @ptrCast(@alignCast(p));
        dm.tag = ctx.value(u32);
        for (&dm.code) |*w| w.* = ctx.value(u32);
    }

    pub fn func_8007A824(ctx: *zd.Ctx, p: anytype) void {
        const dm: *v.DR_MOVE = @ptrCast(@alignCast(p));
        dm.tag = ctx.value(u32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const dm = ctx.alloc(v.DR_MOVE);
    const slot: *align(1) u32 = @ptrFromInt(zd.scratchpad);
    slot.* = psx.addr(dm);
    const rect: ?*v.RECT = if (ctx.chance(4)) null else ctx.alloc(v.RECT);
    const edges = [_]c_int{ 0, 1, -1, 320, 512, 0x7FFF_FFFF, -0x8000_0000 };
    return .{ rect, ctx.pick(c_int, &edges), ctx.pick(c_int, &edges) };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
