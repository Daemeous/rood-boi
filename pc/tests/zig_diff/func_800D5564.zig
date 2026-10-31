//! zig_diff spec: func_800D5564 -> func_800D5564.func_800D5564 (5BF94.c).
//! Inputs: a pFileBlock3 in the arena and a 4 KB arena block whose middle
//! holds the entry, so arg2 in [-16, 16] indexes unk4 inside memory the
//! harness owns. The callee stub returns the valid entry every time and
//! rewrites random unk2 fields (and sometimes the count) through the
//! pointer, so a port that reads the entry before the call disagrees. The C
//! has no branches; the classes split the negative indices from the rest.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D5564));

pub const classes = [_][:0]const u8{ "in-range", "negative-index" };

const Entry = v.func_800D55A4_t;
const esz: isize = @sizeOf(v.func_800D55A4_t2);

var entry_ptr: ?*Entry = null;

fn unk2Off(k: isize) isize {
    return @as(isize, @offsetOf(Entry, "unk4")) + k * esz + @as(isize, @offsetOf(v.func_800D55A4_t2, "unk2"));
}

pub const stubs = struct {
    // The callee always returns the valid entry (the C never tests for NULL,
    // and a NULL here would fault the C). It rewrites an element's unk2 and
    // sometimes the count, which the C reads back after the call.
    pub fn _getPFileBlock3section(ctx: *zd.Ctx, arg0: anytype, arg1: anytype) ?*anyopaque {
        _ = arg0;
        _ = arg1;
        const e = entry_ptr.?;
        const k: isize = @as(isize, ctx.value(u8) % 33) - 16;
        psx.byteAt(u16, e, unk2Off(k)).* = ctx.value(u16);
        if (ctx.value(u1) == 1) psx.byteAt(i32, e, 0).* = ctx.value(i32);
        return e;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const blk: [*]u8 = ctx.allocBytes(4096, 16);
    const entry: *Entry = @ptrCast(@alignCast(blk + 2048));
    entry_ptr = entry;
    const f: *v.pFileBlock3 = ctx.alloc(v.pFileBlock3);
    const arg1: c_int = ctx.pick(c_int, &.{ 0, 1, -1, 0x7fff_ffff, -0x8000_0000 });
    const arg2: c_int = ctx.range(c_int, -16, 16);
    return .{ f, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    return if (args[2] < 0) 1 else 0;
}
