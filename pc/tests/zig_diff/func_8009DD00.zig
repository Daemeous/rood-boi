//! zig_diff spec: func_8009DD00 -> func_8009DD00.func_8009DD00 (30DB0.c).
//! Inputs: an index 0..16 into D_800F4538 (null entries included), a D_800F4538_t
//! in the arena with unk63C and unk6E6 at their edges, arg1 null or an arena
//! SVECTOR, and arg2 at 0, +-1 and INT_MAX. The two callees that return values
//! are stubs: func_8009E228 returns edges around -0xE4, 0xAC and 0x301 (plus
//! random i32s) and writes unk6E6 sometimes; func_8009E180 writes unk63C
//! sometimes, which the C reads back.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009DD00));

pub const classes = [_][:0]const u8{
    "null-entry",
    "ret-below-e4",
    "ret-e4-to-ac",
    "ret-ac-to-301",
    "ret-arg2-zero-28",
    "ret-high-other",
};

// Set by prepare, read by the stubs (the same in both runs).
var ret_e228: i32 = 0;
var e6_value: u16 = 0x28;

pub const stubs = struct {
    pub fn func_8009E180(ctx: *zd.Ctx, p: anytype, arg1: anytype) i32 {
        _ = arg1;
        const o: *v.D_800F4538_t = @ptrCast(@alignCast(p.?));
        if (ctx.value(u2) == 0) o.unk63C = ctx.value(u16);
        return ctx.value(i32);
    }

    pub fn func_8009E228(ctx: *zd.Ctx, p: anytype, arg1: anytype) i32 {
        _ = arg1;
        const o: *v.D_800F4538_t = @ptrCast(@alignCast(p.?));
        if (ctx.value(u2) == 0) o.unk6E6 = e6_value;
        return ret_e228;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    // Index 0..16 only: the table has 17 entries.
    const idx = ctx.edge(c_int, &.{ 0, 1, 8, 15, 16 });
    const tbl = v.g.D_800F4538();
    tbl[@intCast(idx)] = if (ctx.chance(5)) null else obj;

    obj.unk63C = ctx.pick(u16, &.{ 0x80, 0x81, 0x82, 0, 0xFFFF, 0x8000 });
    obj.unk6E6 = if (ctx.chance(2)) 0x28 else ctx.pick(u16, &.{ 0x27, 0x29, 0 });
    e6_value = if (ctx.chance(2)) 0x28 else ctx.pick(u16, &.{ 0x27, 0 });

    ret_e228 = if (ctx.chance(2))
        ctx.edge(i32, &.{ -0xE4, -0xE5, 0xAC, 0xAB, 0x301, 0x300, -1, 0 })
    else
        ctx.int(i32);

    const sv: ?*v.SVECTOR = if (ctx.chance(2)) null else ctx.alloc(v.SVECTOR);
    const arg2 = ctx.edge(c_int, &.{ 0, 0, 1, -1, 0x7FFF_FFFF });
    return .{ idx, sv, arg2 };
}

pub fn classify(args: Args) usize {
    const tbl = v.g.D_800F4538();
    const entry = tbl[@intCast(args[0])] orelse return 0;
    if (ret_e228 < -0xE4) return 1;
    if (ret_e228 < 0xAC) return 2;
    if (ret_e228 < 0x301) return 3;
    if (args[2] == 0 and entry.unk6E6 == 0x28) return 4;
    return 5;
}
