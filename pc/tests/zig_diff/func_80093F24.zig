//! zig_diff spec: func_80093F24 -> func_80093F24.func_80093F24 (2842C.c).
//! Inputs: the argument (0, 1000 and its neighbours, negatives, values whose
//! low 16 bits clamp), a 16-slot D_800F1BAC table in the arena with each
//! slot's u16 tag set to 5 one time in four, and a count from -32768 to 16
//! (the loop only reads the slots it visits). The pointer global is null only
//! when the count is <= 0, where the C never reads through it. func_80096444
//! may rewrite the count; func_800962E4 returns an arena object, since the C
//! never tests its result for null.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80093F24));

pub const classes = [_][:0]const u8{ "arg-zero", "arg-clamped", "no-entries", "match-found", "no-match" };

const slots = 16;
var table: [*]v.D_800F1BAC_t = undefined;
var obj: *v.D_800F1BAC_t = undefined;

pub const stubs = struct {
    pub fn func_80096444(ctx: *zd.Ctx, i: anytype) void {
        _ = i;
        if (ctx.value(u8) < 96) v.g.D_800F227E().* = ctx.value(i16) & 7;
    }
    pub fn func_800962E4(ctx: *zd.Ctx) *v.D_800F1BAC_t {
        _ = ctx;
        return obj;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    table = @ptrCast(@alignCast(ctx.allocBytes(slots * @sizeOf(v.D_800F1BAC_t), 16)));
    obj = ctx.alloc(v.D_800F1BAC_t);
    for (0..slots) |i| {
        table[i].unk8.u16 = if (ctx.chance(4)) 5 else ctx.pick(u16, &.{ 0, 4, 6, 0x0500, 0xFFFB, 0x8005 });
    }
    const count: i16 = if (ctx.chance(5)) ctx.edge(i16, &.{ -32768, -1, 0 }) else ctx.range(i16, 1, slots);
    v.g.D_800F227E().* = count;
    v.g.D_800F1BAC().* = if (count <= 0 and ctx.chance(2)) null else @as(?*anyopaque, @ptrCast(table));
    const arg0 = ctx.pick(c_int, &.{ 0, 1, -1, 999, 1000, 1001, 65536 + 1000, 0x7FFF_FFFF, -2147483648 });
    return .{arg0};
}

pub fn classify(args: Args) usize {
    if (args[0] == 0) return 0;
    const stored: u16 = @truncate(@as(u32, @bitCast(args[0])));
    if (stored >= 1000) return 1;
    const count = v.g.D_800F227E().*;
    if (count <= 0) return 2;
    var i: usize = 0;
    while (i < @as(usize, @intCast(count))) : (i += 1) {
        if (table[i].unk8.u16 == 5) return 3;
    }
    return 4;
}
