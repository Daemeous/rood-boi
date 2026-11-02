//! zig_diff spec: func_800BEC58 -> func_800BEC58.func_800BEC58 (4A0A8.c).
//! Inputs: the trigger type (0xFF, 1 with and without the script-section bit,
//! 0x40, a busy gate, or a scan), the mode and arg2 (a valid arena pointer for
//! mode 1, small actor ids otherwise), and a script table in the arena: 16
//! offsets, each a trigger record of random masks and opcodes laid out with
//! the lengths the C advances by, ended by 0xFF or a failing opcode. About a
//! twelfth of the entries are 0xFFFF terminators, so the loop returns early
//! and also runs off its 16 entries. Operand bytes are small, so equal-sided
//! comparisons and sentinel actor ids happen often. The position out-parameter
//! of the callees that fill it is written by the stubs; getShort reads the
//! script's little-endian bytes (so flag ids are 0 often), func_800BFE50
//! resolves to 0..3 (1: no actor), the state flags and the other queries
//! return small values, zero often.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BEC58));

pub const classes = [_][:0]const u8{ "type-ff", "type-1-section", "type-40", "busy-gate", "scan" };

fn small(ctx: *zd.Ctx) u8 {
    return ctx.range(u8, 0, 3);
}

fn emitOp(ctx: *zd.Ctx, buf: []u8, at: *usize) void {
    const kinds = [_]u8{ 1, 2, 3, 15, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 0xFE, 0x20 };
    const op = kinds[ctx.range(usize, 0, kinds.len - 1)];
    const n: usize = switch (op) {
        1, 4, 8, 16 => 3,
        3, 5, 15 => 4,
        2, 6, 7, 10, 12, 13 => 5,
        11 => 6,
        14 => 7,
        else => 1,
    };
    var b: [7]u8 = undefined;
    b[0] = op;
    for (1..n) |k| b[k] = small(ctx);
    // An actor reference (bit 12 of the short at b[1]) half the time.
    if (op == 4 and ctx.chance(2)) b[2] |= 0x10;
    if (op == 3 or op == 15) {
        b[1] = (ctx.range(u8, 0, 7) << 4) | @as(u8, ctx.int(u4));
    }
    @memcpy(buf[at.* .. at.* + n], b[0..n]);
    at.* += n;
}

fn emitScript(ctx: *zd.Ctx, buf: []u8, at: *usize) void {
    buf[at.*] = ctx.int(u8);
    at.* += 1;
    std.mem.writeInt(u16, buf[at.*..][0..2], ctx.pick(u16, &.{ 0, 0x8000, 0x7FFF, 0xFFFF, 0x800F }), .little);
    at.* += 2;
    const nops = ctx.range(usize, 0, 4);
    for (0..nops) |_| emitOp(ctx, buf, at);
    buf[at.*] = 0xFF;
    at.* += 1;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const base = ctx.allocBytes(0x8000, 16);
    const buf = base[0..0x8000];
    var at: usize = 32;
    var i: usize = 0;
    while (i < 16) : (i += 1) {
        if (ctx.chance(12)) {
            std.mem.writeInt(u16, buf[2 * i ..][0..2], 0xFFFF, .little);
            break;
        }
        std.mem.writeInt(u16, buf[2 * i ..][0..2], @intCast(at), .little);
        emitScript(ctx, buf, &at);
    }

    const raw: *anyopaque = @ptrCast(base);
    v.g._mpdClearedSection().* = raw;
    v.g.D_800F4C34().* = ctx.pick(u8, &.{ 0, 1, 2 });
    v.g.D_800F4BA8().* = if (ctx.chance(4)) ctx.edge(u8, &.{ 1, 0x80 }) else 0;
    v.g._mpdScriptSection().* = ctx.pick(i16, &.{ 0, 1, 2, 3, -1, -2 });

    const mode: c_int = ctx.edge(c_int, &.{ 0, 1, 2, -1, 0x10000, 0x10001 });
    const ptrbuf = ctx.allocBytes(16, 16);
    ptrbuf[0] = ctx.range(u8, 0, 3);
    ptrbuf[2] = ctx.range(u8, 0, 3);
    const arg2: c_int = if (@as(i16, @truncate(mode)) == 1) // the C reads mode as a short
        @bitCast(@as(u32, @truncate(@intFromPtr(ptrbuf))))
    else
        ctx.pick(c_int, &.{ 0, 1, 2, 3 });

    const arg0 = ctx.pick(c_int, &.{ 0xFF, 1, 0x40, 0, 2, 0x80, 0x41, 0x1FF, -1, 0x10001 });
    const arg1 = ctx.pick(c_int, &.{ 0, 1, 2, 3, -1, 0x40 });
    return .{ arg0, arg1, arg2, mode };
}

pub fn classify(args: Args) usize {
    const t: u16 = @truncate(@as(u32, @bitCast(args[0])));
    if (t == 0xFF) return 0;
    if (t == 1 and (v.g._mpdScriptSection().* & 1) != 0) return 1;
    if (t == 0x40) return 2;
    if (v.g.D_800F4C34().* != 0 and v.g.D_800F4BA8().* != 0) return 3;
    return 4;
}

fn outOf(out: anytype) *v.func_8006EBF8_t {
    const raw: *anyopaque = if (@typeInfo(@TypeOf(out)) == .optional) out.? else out;
    return @ptrCast(@alignCast(raw));
}

fn writePos(ctx: *zd.Ctx, out: anytype) void {
    const pos = outOf(out);
    pos.unk0.unk0._bytes[0] = ctx.value(u2);
    pos.unk0.unk0._bytes[2] = ctx.value(u2);
    pos.unk0.unk0._bytes[3] = ctx.value(u4);
    pos.unk0.unk4.vy = one(ctx, i16, &.{ -256, -255, -129, -128, -127, -1, 0, 1, 127, 128, 129, 255, 256, -32768, 32767 });
}

/// One of `edges`, drawn as a stub's value (the same in the C and the Zig run).
fn one(ctx: *zd.Ctx, comptime T: type, edges: []const T) T {
    return edges[ctx.value(u32) % edges.len];
}

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = ctx;
        const b: [*]const u8 = @ptrCast(p);
        return @bitCast(@as(u16, b[0]) | @as(u16, b[1]) << 8);
    }
    pub fn func_800BFE50(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        return one(ctx, c_int, &.{ 0, 1, 1, 2, 3, 0x10001, -1 });
    }
    pub fn vs_battle_getStateFlag(ctx: *zd.Ctx, id: anytype) u8 {
        _ = id;
        return one(ctx, u8, &.{ 0, 1, 2, 3, 0x80, 0xFF });
    }
    pub fn func_8007C218(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        return one(ctx, c_int, &.{ 0, 0, 1, 0x10000, -1 });
    }
    pub fn func_800A92B8(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = a;
        _ = b;
        return one(ctx, c_int, &.{ 0, 0, 127, 128, -128, -127, -129, 0x7FFFFF80 });
    }
    pub fn func_8007CFCC(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        return one(ctx, c_int, &.{ 0, 0, 1, -1 });
    }
    pub fn func_800A0BE0(ctx: *zd.Ctx, id: anytype) c_uint {
        _ = id;
        return one(ctx, c_uint, &.{ 0, 0x4000, 0x100000, 0x104000, 0xFFFFFFFF });
    }
    pub fn func_800A1108(ctx: *zd.Ctx, id: anytype, out: anytype) c_int {
        _ = id;
        writePos(ctx, out);
        return ctx.value(c_int);
    }
    pub fn func_800A0024(ctx: *zd.Ctx, id: anytype, out: anytype) c_int {
        _ = id;
        writePos(ctx, out);
        return @as(c_int, ctx.value(u2)) - 1;
    }
    pub fn func_8007CF80(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        return one(ctx, c_int, &.{ 0, 0, 1, 0x10000, -1 });
    }
    pub fn func_8007CF18(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        if (ctx.value(u1) == 1) return 1;
        return ctx.value(c_int);
    }
    pub fn func_8007CF64(ctx: *zd.Ctx, id: anytype) ?*c.vs_battle_actor {
        _ = id;
        if (ctx.value(u1) == 0) return null;
        return @ptrCast(@alignCast(&zd.zdiff_arena));
    }
    pub fn vs_battle_itemIdIsInInventory(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        return one(ctx, c_int, &.{ 0, 0, 1, 0x10000, -1 });
    }
};
