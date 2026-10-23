//! zig_diff spec: func_80093914 -> func_80093914.func_80093914 (2842C.c).
//! Inputs: the room's geometry section is a buffer in the arena. The entry
//! record (func_8009195C's result) holds a block offset at [7] and a second,
//! different valid block offset at [6], so a port that reads the wrong slot
//! works on a real block and differs in the state. The offset is negative or
//! positive into that buffer, or the entry is NULL. Each block holds the two
//! counts n0 and n1, from -2^31 through 4, so each loop runs zero or more
//! times. The stub may repoint entry[7] at a second valid block, which the C
//! reads back after the call. The blocks' random t/q records are swapped in
//! place.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80093914));

pub const classes = [_][:0]const u8{ "null-entry", "negative-offset", "no-blocks", "t-only", "q-only", "both" };

var g_ep: [*]i32 = undefined;
var g_null: bool = false;
var g_off: i32 = 0;
var g_alt: i32 = 0;
var g_other: i32 = 0;
var g_n0: i32 = 0;
var g_n1: i32 = 0;

fn writeBlock(base: [*]u8, off: i32, n0: i32, n1: i32) void {
    psx.byteAt(i32, base, @as(isize, off)).* = n0;
    psx.byteAt(i32, base, @as(isize, off) + 4).* = n1;
}

pub const stubs = struct {
    pub fn func_8009195C(ctx: *zd.Ctx, arg0: anytype) ?*i32 {
        _ = arg0;
        if (g_null) return null;
        // The callee may repoint the block offset; the C re-reads entry[7].
        if (ctx.value(u1) == 1) g_ep[7] = g_alt;
        return @ptrCast(g_ep);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const room = v.g.vs_battle_roomData();
    const pad: [*]u8 = ctx.allocBytes(8192, 16);
    const base: [*]u8 = pad + 4096;
    const basep: *i32 = @ptrCast(@alignCast(base));
    psx.put(&room.geometrySection, basep);

    g_ep = @ptrCast(@alignCast(ctx.allocBytes(32, 16)));
    g_off = ctx.edge(i32, &.{ -4096, -16, -4, 0, 4, 8, 16, 100, 1000 });
    g_alt = ctx.edge(i32, &.{ 0, 4, 8, 12, 40, 200 });
    g_other = ctx.edge(i32, &.{ -16, 0, 4, 8, 12, 40, 200, 400 });
    g_n0 = ctx.edge(i32, &.{ -2147483648, -1, 0, 0, 1, 2, 3, 4 });
    g_n1 = ctx.edge(i32, &.{ -1, 0, 0, 1, 2, 3 });
    writeBlock(base, g_off, g_n0, g_n1);
    writeBlock(base, g_alt, g_n0, g_n1);
    writeBlock(base, g_other, g_n0, g_n1);
    g_ep[6] = g_other;
    g_ep[7] = g_off;
    g_null = ctx.chance(8);

    return .{ctx.int(c_int)};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (g_null) return 0;
    if (g_off < 0) return 1;
    if (g_n0 <= 0 and g_n1 <= 0) return 2;
    if (g_n0 > 0 and g_n1 <= 0) return 3;
    if (g_n0 <= 0 and g_n1 > 0) return 4;
    return 5;
}
