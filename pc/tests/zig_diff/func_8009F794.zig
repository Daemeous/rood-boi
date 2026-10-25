//! zig_diff spec: func_8009F794 -> func_8009F794.func_8009F794 (30DB0.c).
//! Inputs: the argument object (always non-null, since the C reads its unk5C
//! unconditionally) and the 16-slot table D_800F45E0 holding nulls, candidates,
//! and sometimes the argument itself. A candidate passes or fails each test in
//! turn: unk8_0, unk1A, unk6C[8].unk0_3, unk12 and the unk5C word against the
//! argument's. unk1E is drawn from a small edge set (including -30000, the
//! sentinel, and -30001, -32768) so the ordered comparisons see equal sides
//! often and the signed compare is exercised with negative values.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
const psx = @import("common").psx;
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009F794));

pub const classes = [_][:0]const u8{ "all-null", "self-in-table", "candidates", "full-table" };

var mode: usize = 0;

fn fill(ctx: *zd.Ctx, o: *v.D_800F45E0_t, t1: u32) void {
    psx.setBits(o, "unk8_0", if (ctx.chance(4)) 1 else 0);
    o.unk1A = ctx.edge(i16, &.{ 0, 0, 0, 1, -1 });
    psx.setBits(&o.unk6C[8], "unk0_3", if (ctx.chance(4)) 1 else 0);
    o.unk12 = ctx.edge(u8, &.{ 0xFF, 0xFF, 0xFF, 0xFE, 0 });
    o.unk1E = ctx.edge(i16, &.{ -30000, -30001, -32768, 0, 1, -1, 32767, 6 });
    psx.byteAt(u32, o, 0x5C).* = if (ctx.chance(5)) ctx.int(u32) else t1;
}

fn newObj(ctx: *zd.Ctx, t1: u32) *v.D_800F45E0_t {
    const o = ctx.alloc(v.D_800F45E0_t);
    fill(ctx, o, t1);
    return o;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.D_800F45E0_t);
    const t1 = ctx.pick(u32, &.{ 0, 0xFFFF_FFFF, 0x8000_0000, 0x0101_0101 });
    fill(ctx, arg, t1);
    mode = ctx.range(usize, 0, 3);
    const table = v.g.D_800F45E0();
    for (table) |*slot| {
        const pick: ?*v.D_800F45E0_t = switch (mode) {
            0 => null,
            1 => if (ctx.chance(3)) arg else if (ctx.chance(2)) null else newObj(ctx, t1),
            2 => if (ctx.chance(4)) null else newObj(ctx, t1),
            else => newObj(ctx, t1),
        };
        slot.* = pick;
    }
    return .{arg};
}

pub fn classify(args: Args) usize {
    _ = args;
    return mode;
}
