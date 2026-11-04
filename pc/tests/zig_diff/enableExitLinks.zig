//! zig_diff spec: func_800DC638 -> pc.exit_links.enableExitLinks (D2: pc/src/navigation.c).
//! Inputs: the link table D_800F58C0 (0x400 bytes in the arena, 0 a tenth of
//! the runs), each word a mix of the tested bits (0x04000400, 0x400 alone, the
//! 0x04000000 bit alone, neither) with random coordinate fields, and the
//! eight tile words at D_800F5910 whose x/z fields (low 10 bits) are drawn from
//! the same small set as the table's, so the equalities hold often. The stub of
//! func_8008D2C0 writes 0-13 exit records (halfword 3 any value, as it is
//! `& 31`) and returns the count, or -2 (reported by the stub of
//! pc_unimplemented, which returns: the loop then does nothing). A count above
//! 13 (14..16, with a null table so the stack leftovers it reads go unused) is reported too.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_navigation");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").enableExitLinks));

pub const classes = [_][:0]const u8{ "no-table", "table-no-flags", "table-flags", "negative-count" };

var count: c_int = 0;

/// pc_unimplemented's message: the C passes its literal, the port its own (the
/// same bytes at another address), so it is compared by its text.
pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "pc_unimplemented");
}

fn coordinates(ctx: *zd.Ctx) u32 {
    // x in bits 5-9, z in bits 0-4 (and the same fields at 21-25 and 16-20 of a link word).
    return ctx.edge(u32, &.{ 0, 1, 2, 0x21, 0x3E0, 0x3FF });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    // A count above 13 makes the C read past its buffer (stack leftovers into
    // `active`); that is harmless only when no table is scanned, so it is drawn
    // together with a null table.
    const overflow = ctx.chance(12);
    count = if (overflow) ctx.range(c_int, 14, 16) else if (ctx.chance(12)) -2 else ctx.range(c_int, 0, 13);
    const table: [*]u32 = @ptrCast(@alignCast(ctx.allocBytes(0x400, 16)));
    // A sixth of the runs have a table with no tested bit at all.
    const plain = ctx.chance(6);
    for (0..0x100) |i| {
        var w: u32 = switch (ctx.range(u8, 0, 4)) {
            0 => ctx.int(u32),
            1 => ctx.int(u32) & ~@as(u32, 0x04000400),
            2 => (ctx.int(u32) & ~@as(u32, 0x04000400)) | 0x400,
            3 => (ctx.int(u32) & ~@as(u32, 0x04000400)) | 0x04000000,
            else => ctx.int(u32) | 0x04000400,
        };
        // Coordinates drawn from the small set, so the link words can match them.
        if (ctx.chance(2)) w = (w & ~@as(u32, 0x03FF03FF)) | coordinates(ctx) | (coordinates(ctx) << 16);
        table[i] = if (plain) w & ~@as(u32, 0x04000400) else w;
    }
    if (ctx.chance(3)) table[0] = (table[0] & ~@as(u32, 0x04000400)) | 0x04000000;
    const links: [*]u32 = @ptrCast(@alignCast(ctx.allocBytes(8 * 4, 16)));
    for (0..8) |i| links[i] = if (ctx.chance(3)) ctx.int(u32) else coordinates(ctx);
    v.g.D_800F58C0().* = if (overflow or ctx.chance(10)) 0 else @intCast(@intFromPtr(table));
    v.g.D_800F5910().* = @intCast(@intFromPtr(links));
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (count < 0) return 3;
    if (v.g.D_800F58C0().* == 0) return 0;
    const table: [*]const u32 = @ptrFromInt(v.g.D_800F58C0().*);
    for (0..0x100) |i| if (table[i] & 0x04000400 != 0) return 2;
    return 1;
}

pub const stubs = struct {
    pub fn func_8008D2C0(ctx: *zd.Ctx, p: anytype) c_int {
        if (count > 0) {
            const records: [*][4]u16 = @ptrCast(@alignCast(p.?));
            for (0..@intCast(@min(count, 13))) |i| {
                records[i] = .{ ctx.value(u16), ctx.value(u16), ctx.value(u16), ctx.value(u16) };
            }
        }
        return count;
    }

    pub fn pc_unimplemented(ctx: *zd.Ctx, message: anytype) void {
        _ = ctx;
        _ = message;
    }
};
