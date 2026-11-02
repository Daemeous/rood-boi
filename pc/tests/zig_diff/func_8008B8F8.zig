//! zig_diff spec: func_8008B8F8 -> func_8008B8F8.func_8008B8F8 (146C.c).
//! Inputs: no entry, or a random 12-byte arena entry; _getDoorId's stub
//! returns edge values (-1, ids, values that wrap at 16 bits). memcpy and
//! bzero are recording stubs: they copy and clear as the game's do.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8008B8F8));

pub const classes = [_][:0]const u8{ "none", "entry" };

pub const stubs = struct {
    pub fn vs_main_memcpy(_: *zd.Ctx, dest: anytype, src: anytype, size: anytype) void {
        const d: [*]u8 = @ptrCast(dest.?);
        const s: [*]const u8 = @ptrCast(src.?);
        @memcpy(d[0..size], s[0..size]);
    }
    pub fn vs_main_bzero(_: *zd.Ctx, dest: anytype, size: anytype) void {
        const d: [*]u8 = @ptrCast(dest.?);
        @memset(d[0..@intCast(size)], 0);
    }
    pub fn _getDoorId(ctx: *zd.Ctx, door: anytype) c_int {
        _ = door;
        return ctx.edge(c_int, &.{ -1, 0, 1, 7, 0x7FFF, 0x8000, 0x12345 });
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(3)) return .{null};
    return .{@as(*[12]u8, @ptrCast(ctx.allocBytes(12, 4)))};
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
