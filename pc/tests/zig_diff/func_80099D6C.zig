//! zig_diff spec: func_80099D6C -> func_80099D6C.func_80099D6C (30DB0.c).
//! Inputs: an arg0 that is in range (0..15, sometimes the table's last slot),
//! at 16 or past it (16 is the `>=` equal case), or the int maximum. The
//! 16-slot D_800F45E0 table holds random mixes of three arena objects (null,
//! aliased, or distinct), so the actorId scan finds its match below, above,
//! or at slot 15, or finds none. The object's unk0 is zero about one time in
//! six, and nonzero values cover both sign bits. Negative arg0 is not built:
//! the C would index before the table, which the harness does not own.
//! func_800E68A0 is a stub that rewrites the object's unk0. vs_main_freeHeap
//! records its argument and leaves the state alone; the C does not read
//! anything back from either call.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80099D6C));

pub const classes = [_][:0]const u8{
    "out-of-range",
    "no-object",
    "inactive",
    "not-found",
    "found-below",
    "found-above",
    "found-at-15",
};

pub const stubs = struct {
    pub fn func_800E68A0(ctx: *zd.Ctx, p: anytype) void {
        const a: *v.D_800F45E0_t = @ptrCast(@alignCast(p.?));
        a.unk0 = ctx.value(u8);
    }
};

fn newObject(ctx: *zd.Ctx) *v.D_800F45E0_t {
    const o = ctx.alloc(v.D_800F45E0_t);
    psx.put(&o.unk68, ctx.alloc(v.D_800F45E0_unk68));
    o.unk0 = if (ctx.chance(6)) 0 else ctx.range(u8, 1, 255);
    return o;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const objs = [3]*v.D_800F45E0_t{ newObject(ctx), newObject(ctx), newObject(ctx) };
    const id0 = ctx.range(u3, 0, 7);
    psx.setBits(&objs[0].unk6C[8], "actorId", id0);
    for (objs[1..]) |o| {
        const id = if (ctx.chance(3)) id0 else id0 +% ctx.range(u3, 1, 7);
        psx.setBits(&o.unk6C[8], "actorId", id);
    }

    const table = v.g.D_800F45E0();
    const isolate = ctx.chance(3);
    for (table) |*slot| {
        slot.* = if (isolate or ctx.chance(2)) null else objs[ctx.range(usize, 0, 2)];
    }
    const partner: usize = if (ctx.chance(4)) 15 else ctx.range(usize, 0, 15);
    if (ctx.chance(2)) table[partner] = objs[0];

    const arg: c_int = if (ctx.chance(5))
        ctx.edge(c_int, &.{ 16, 17, 255, 0x7fff_ffff })
    else
        ctx.range(c_int, 0, 15);
    if (arg < 16) table[@intCast(arg)] = if (ctx.chance(8)) null else objs[0];
    return .{arg};
}

pub fn classify(args: Args) usize {
    const arg = args[0];
    if (arg >= 16) return 0;
    const index: usize = @intCast(arg);
    const table = v.g.D_800F45E0();
    const temp = table[index] orelse return 1;
    if (temp.unk0 == 0) return 2;
    const id = psx.getBits(&temp.unk6C[8], "actorId");
    for (table, 0..) |slot, i| {
        if (i == index) continue;
        const o = slot orelse continue;
        if (psx.getBits(&o.unk6C[8], "actorId") != id) continue;
        if (i < index) return 4;
        return if (i == 15) 6 else 5;
    }
    return 3;
}
