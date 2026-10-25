//! zig_diff spec: func_8009FB64 -> func_8009FB64.func_8009FB64 (30DB0.c).
//! Inputs: a VECTOR in the arena (random bytes), the 17-entry D_800F4538 table
//! all null, all pointing at one of two arena objects, or mixed, and
//! D_800F2458.unk1FE0 at 0, 1, 0x7FFF, 0x8000, 0xFFFF or random. One class
//! makes arg0 alias the unk183C field of a table object, which the loop
//! writes, so a copy made after the loop reads the stored value.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009FB64));

pub const classes = [_][:0]const u8{ "all-null", "mixed", "all-set", "arg-aliases-store" };

fn aliasOf(obj: *v.D_800F4538_t) *v.VECTOR {
    const base: [*]u8 = @ptrCast(obj);
    return @ptrCast(@alignCast(base + 0x183C));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    v.g.D_800F2458().unk1FE0 = ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFF });
    const objs = [2]*v.D_800F4538_t{ ctx.alloc(v.D_800F4538_t), ctx.alloc(v.D_800F4538_t) };
    const mode = ctx.range(u8, 0, 9);
    for (0..17) |i| {
        const entry: ?*v.D_800F4538_t = if (mode < 2)
            null
        else if (mode == 2)
            objs[ctx.range(usize, 0, 1)]
        else if (ctx.chance(2))
            null
        else
            objs[ctx.range(usize, 0, 1)];
        table[i] = entry;
    }
    const vec: *v.VECTOR = if (ctx.chance(4)) blk: {
        table[ctx.range(usize, 0, 16)] = objs[0];
        break :blk aliasOf(objs[0]);
    } else ctx.alloc(v.VECTOR);
    return .{vec};
}

pub fn classify(args: Args) usize {
    const a = @intFromPtr(args[0].?);
    const table = v.g.D_800F4538();
    var n: usize = 0;
    for (table) |e| {
        if (e) |obj| {
            const base = @intFromPtr(obj) + 0x183C;
            if (a >= base and a < base + 16) return 3;
            n += 1;
        }
    }
    if (n == 0) return 0;
    if (n == 17) return 2;
    return 1;
}
