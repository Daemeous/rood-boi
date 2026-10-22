//! zig_diff spec: _parsePfileBlock13 -> parse_pfile_block13.parsePfileBlock13 (6F014.c).
//! The C has no branches of its own and no value tests. Its inputs are the
//! block pointer `arg0`, in four classes: null, a pointer into the arena, a
//! pointer with the top address bit set (under 4 GB, so `psx_addr` is valid),
//! and any other address. The global `D_800F569C` always points at an arena
//! struct, because the C dereferences it without a test. `func_80046608` is
//! a recording stub that returns a value (with its sign bit both set and
//! clear); it is assumed to change no state the C reads back.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_6F014");
const c = @import("c_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").parsePfileBlock13));

pub const classes = [_][:0]const u8{ "null", "arena", "high-address", "other" };

pub const stubs = struct {
    pub fn func_80046608(ctx: *zd.Ctx, arg0: anytype) c_int {
        _ = arg0;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const d = ctx.alloc(v.D_800F569C_t);
    v.g.D_800F569C().* = d;
    const choice = ctx.range(u8, 0, 3);
    const arg: ?*anyopaque = switch (choice) {
        0 => null,
        1 => @ptrCast(ctx.allocBytes(16, 16)),
        2 => @ptrFromInt(zd.low_ram + ctx.range(u32, 0, 0x7FFF)),
        else => @ptrFromInt(@as(usize, ctx.range(u32, 1, 0x7FFF_FFFF))),
    };
    return .{arg};
}

pub fn classify(args: Args) usize {
    const p = @intFromPtr(args[0]);
    if (p == 0) return 0;
    const base = @intFromPtr(&zd.zdiff_arena[0]);
    if (p >= base and p < base + zd.arena_size) return 1;
    if (p >= 0x8000_0000) return 2;
    return 3;
}
