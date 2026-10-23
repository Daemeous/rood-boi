//! zig_diff spec: func_800A4D8C -> func_800A4D8C.func_800A4D8C (3A1A0.c).
//! Inputs: no arguments. The 16-entry D_800F45E0 table and the 17-entry D_800F4538
//! table are each filled with null or one of a few arena objects. Those objects
//! carry unk0 zero or nonzero (D_800F45E0) and skip set or clear (D_800F4538).
//! Some runs are "quiet" (every live entry dormant), and the tick count
//! (vs_gametime_tickspeed) takes edges including negative odd values, where
//! `/ 2` and `>> 1` differ. The callees write back unk0, skip bits and table
//! entries, so a port that reads them before the call still differs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A4D8C));

var objs4538: [3]*v.D_800F4538_t = undefined;
var objs45e0: [3]*v.D_800F45E0_t = undefined;

pub const classes = [_][:0]const u8{ "idle", "negative-tick", "stepped", "only-skipped" };

pub const stubs = struct {
    pub fn func_800B0908(ctx: *zd.Ctx, p: anytype, ticks: anytype) void {
        _ = ticks;
        const o: *v.D_800F45E0_t = @ptrFromInt(@intFromPtr(p));
        if (ctx.value(u1) == 1) o.unk0 = ctx.value(u8);
        if (ctx.value(u2) == 0) psx.setBits(&objs4538[ctx.value(u8) % 3].unk0, "skip", ctx.value(u1));
    }

    pub fn func_800A4E68(ctx: *zd.Ctx, i: anytype) void {
        _ = i;
        const tbl = v.g.D_800F4538();
        const idx = ctx.value(u8) % 17;
        switch (ctx.value(u2)) {
            0 => tbl[idx] = null,
            1 => tbl[idx] = objs4538[ctx.value(u8) % 3],
            2 => psx.setBits(&objs4538[ctx.value(u8) % 3].unk0, "skip", ctx.value(u1)),
            3 => {},
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const quiet = ctx.chance(5);
    for (&objs4538) |*slot| {
        const o = ctx.alloc(v.D_800F4538_t);
        psx.setBits(&o.unk0, "skip", if (quiet or ctx.chance(3)) 1 else 0);
        slot.* = o;
    }
    for (&objs45e0) |*slot| {
        const o = ctx.alloc(v.D_800F45E0_t);
        o.unk0 = if (quiet or ctx.chance(3)) 0 else ctx.pick(u8, &.{ 1, 0x80, 0xFF });
        slot.* = o;
    }
    for (v.g.D_800F45E0()) |*e| {
        e.* = if (ctx.chance(4)) null else objs45e0[ctx.range(usize, 0, 2)];
    }
    for (v.g.D_800F4538()) |*e| {
        e.* = if (ctx.chance(4)) null else objs4538[ctx.range(usize, 0, 2)];
    }
    v.g.vs_gametime_tickspeed().* = ctx.pick(i32, &.{
        0,                  1,  -1, 2, -2, 3, -3, 7, -7,
        std.math.maxInt(i32), std.math.minInt(i32),
    });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    var stepped = false;
    for (v.g.D_800F4538()) |e| {
        if (e) |o| {
            if (psx.getBits(&o.unk0, "skip") == 0) stepped = true;
        }
    }
    var any45 = false;
    for (v.g.D_800F45E0()) |e| {
        if (e) |o| {
            if (o.unk0 != 0) any45 = true;
        }
    }
    if (!stepped and !any45) return 0;
    if (v.g.vs_gametime_tickspeed().* < 0) return 1;
    if (stepped) return 2;
    return 3;
}
