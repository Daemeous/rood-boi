//! zig_diff spec: _parsePfileBlock11 -> parse_pfile_block11.parsePfileBlock11 (6F014.c).
//! Inputs: a D_800F569C_t in the arena, set as the global block pointer, and a
//! byte pointer that is either an arena block or a spot in PS1 low RAM (mapped
//! at 0x80000000). The C has no branches of its own; it stores psx_addr(arg0)
//! into block11Data.raw, so the spec checks that the stored guest address
//! matches for both pointer regions, and that the store lands in the same
//! field of the same object.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").parsePfileBlock11));

pub const classes = [_][:0]const u8{ "arena", "low-ram" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.alloc(v.D_800F569C_t);
    v.g.D_800F569C().* = block;
    if (ctx.chance(2)) {
        const off: usize = ctx.range(usize, 0, 0x7000);
        return .{@as([*c]u8, @ptrFromInt(zd.low_ram + off))};
    }
    return .{@as([*c]u8, @ptrCast(ctx.allocBytes(64, 16)))};
}

pub fn classify(args: Args) usize {
    const addr: usize = @intFromPtr(args[0]);
    return if (addr >= zd.low_ram and addr < zd.low_ram + zd.low_ram_size) 1 else 0;
}
