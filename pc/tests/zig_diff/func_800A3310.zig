//! zig_diff spec: func_800A3310 -> func_800A3310.func_800A3310 (3A1A0.c).
//! Inputs: an arg0 from -3 to 17 (the table has 17 entries, so the loop never
//! reads past it), and a random SVECTOR. Four D_800F4538_t objects sit in the
//! arena with random unkA_5 bits and small-edge positions; each of the 17
//! table entries is null or one of them. Half the vectors copy an object's
//! vx/vy (and usually vz), so the position compares see equal sides often,
//! and some copies break only vy or only vz, so the partial compares matter.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A3310));

pub const classes = [_][:0]const u8{ "no-loop", "found", "not-found" };

const edges = [_]i16{ 0, 1, -1, 32767, -32768 };

pub fn prepare(ctx: *zd.Ctx) Args {
    const objs = [_]*v.D_800F4538_t{
        ctx.alloc(v.D_800F4538_t),
        ctx.alloc(v.D_800F4538_t),
        ctx.alloc(v.D_800F4538_t),
        ctx.alloc(v.D_800F4538_t),
    };
    for (objs) |o| {
        psx.setBits(&o.unk0, "unkA_5", @intFromBool(ctx.chance(2)));
        o.unk0.position.vx = ctx.pick(i16, &edges);
        o.unk0.position.vy = ctx.pick(i16, &edges);
        o.unk0.position.vz = ctx.pick(i16, &edges);
        o.unk0.position.pad = ctx.int(i16);
    }

    // Every table entry is set: the C reads only some, but a random pointer
    // in any entry it reads would fault.
    const tbl = v.g.D_800F4538();
    for (tbl) |*e| {
        e.* = if (ctx.chance(5)) null else objs[ctx.range(usize, 0, objs.len - 1)];
    }

    // (the edges only: ctx.pick draws any c_int half the time, and the C
    // indexes the 17-entry table up to arg0 unchecked)
    const ns = [_]c_int{ 0, 1, 2, 3, 16, 17, -1 };
    const n: c_int = if (ctx.chance(3))
        ns[ctx.range(usize, 0, ns.len - 1)]
    else
        ctx.range(c_int, -3, 17);

    const vec = ctx.alloc(v.SVECTOR);
    vec.vx = ctx.pick(i16, &edges);
    vec.vy = ctx.pick(i16, &edges);
    vec.vz = ctx.pick(i16, &edges);
    vec.pad = ctx.int(i16);
    if (ctx.chance(2)) {
        const src = objs[ctx.range(usize, 0, objs.len - 1)].unk0.position;
        vec.vx = src.vx;
        vec.vy = if (ctx.chance(3)) ctx.int(i16) else src.vy;
        vec.vz = if (ctx.chance(3)) ctx.int(i16) else src.vz;
    }

    return .{ n, vec };
}

fn hit(e: ?*v.D_800F4538_t, vec: *v.SVECTOR) bool {
    const a = e orelse return false;
    if (psx.getBits(&a.unk0, "unkA_5") == 0) return false;
    return a.unk0.position.vx == vec.vx and
        a.unk0.position.vy == vec.vy and
        a.unk0.position.vz == vec.vz;
}

pub fn classify(args: Args) usize {
    const n = args[0];
    if (n <= 2) return 0;
    const tbl = v.g.D_800F4538();
    // Walk the table itself (17 entries), so no index can leave it.
    for (tbl, 0..) |e, idx| {
        const i: c_int = @intCast(idx);
        if (i < 2 or i >= n) continue;
        if (hit(e, args[1].?)) return 1;
    }
    return 2;
}
