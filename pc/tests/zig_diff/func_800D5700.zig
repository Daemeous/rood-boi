//! zig_diff spec: func_800D5700 -> func_800D5700.func_800D5700 (5BF94.c).
//! Inputs: a func_800D5780_t in the arena whose unk0 points at a table of
//! func_800D57FC_t entries inside an 8 KB arena block, 4 KB in, so indices
//! -1024..1023 stay inside the block (negative ones read below the table: the
//! signed index). Some runs take unk6 from the edges -1024, -1, 0, 1023. The
//! alias case is unk6 = 0 with the table placed on the object's own unkC, so
//! the store to unkC rewrites the bytes unkA is read from: the store order matters.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D5700));

pub const classes = [_][:0]const u8{ "alias", "neg-index", "pos-index" };

var alias_case: bool = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.func_800D5780_t);
    const blk = ctx.allocBytes(8192, 16);
    alias_case = ctx.chance(4);
    var base_addr: usize = @intFromPtr(blk) + 4096;
    if (alias_case) {
        arg.unk6 = 0;
        base_addr = @intFromPtr(arg) + @offsetOf(v.func_800D5780_t, "unkC");
    } else if (ctx.chance(8)) {
        arg.unk6 = ctx.edge(i16, &.{ -1024, -1, 0, 1023 });
    } else {
        arg.unk6 = ctx.range(i16, -1024, 1023);
    }
    const base: *v.func_800D57FC_t = @ptrFromInt(base_addr);
    psx.put(&arg.unk0, base);
    return .{arg};
}

pub fn classify(args: Args) usize {
    if (alias_case) return 0;
    return if (args[0].?.unk6 < 0) 1 else 2;
}
