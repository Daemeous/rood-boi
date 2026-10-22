//! zig_diff spec: _parsePfileBlock12 -> parse_pfile_block12.parsePfileBlock12 (6F014.c).
//! Inputs: the block pointer arg0 is null, the PS1 low-RAM address (0x80000000,
//! a valid guest address), or a fresh arena block. The global D_800F569C points
//! at its own arena block with random contents. The unit stores psx_addr(arg0)
//! into block12Data and passes the same guest address to func_80046168, whose
//! result is unused. Nothing in the C branches, so the classes split the null
//! and non-null guest addresses.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").parsePfileBlock12));

pub const classes = [_][:0]const u8{ "null-arg", "low-ram-arg", "arena-arg" };

pub const stubs = struct {
    // The callee takes the guest address and may change anything, but the C
    // reads nothing after the call, so the stub only returns a value.
    pub fn func_80046168(ctx: *zd.Ctx, arg: anytype) c_int {
        _ = arg;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F569C().* = ctx.alloc(v.D_800F569C_t);
    const choice = ctx.range(u8, 0, 3);
    const block: ?*anyopaque = switch (choice) {
        0 => null,
        1 => @as(?*anyopaque, @ptrFromInt(zd.low_ram)),
        else => @ptrCast(ctx.allocBytes(64, 16)),
    };
    return .{block};
}

pub fn classify(args: Args) usize {
    const p = args[0] orelse return 0;
    return if (@intFromPtr(p) == zd.low_ram) 1 else 2;
}
