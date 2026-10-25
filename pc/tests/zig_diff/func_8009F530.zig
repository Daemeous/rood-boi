//! zig_diff spec: func_8009F530 -> func_8009F530.func_8009F530 (30DB0.c).
//! Inputs: arg0 is a table slot (0-15) that always holds an object. The other
//! slots are objects or null (non-twin), or all share one template (twin): the
//! loop's full match path, with unk1E drawn from {-1, 0, 1} so the `<=` sees
//! equal sides. The stubs return -1 or a live slot from func_8009F794 (which may
//! rewrite the arg0 object's unk12), a slot index from func_800A91DC, null or one
//! of two pool objects from func_800A3C00 (which may rewrite the pool object's
//! unkF, the loop's next index, and unkA_0), and func_800E68A0 may rewrite
//! unk6C[8].unk0_4, which the C tests after the call.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009F530));

pub const classes = [_][:0]const u8{ "twin-table", "full-table", "null-slot" };

/// Set by prepare: the table holds identical twins (no null slots).
var twin_mode: bool = false;
/// The objects func_800A3C00 hands out (allocated by prepare).
var pool: [2]*v.D_800F4538_t = undefined;

fn fill(ctx: *zd.Ctx, e: *v.D_800F45E0_t, t5C: u8, t5D: u8, t5E: u8) void {
    psx.setBits(&e.unk6C[8], "unk0_4", ctx.int(u2));
    if (twin_mode) {
        e.unk5C = t5C;
        e.unk5D = t5D;
        e.unk5E = t5E;
        e.unk1A = 0;
        e.unk12 = 0xFF;
        e.unk1E = ctx.edge(i16, &.{ -1, 0, 1 });
        psx.setBits(e, "unk8_0", 0);
        psx.setBits(&e.unk6C[8], "unk0_3", 0);
    } else {
        e.unk5C = if (ctx.chance(2)) t5C else ctx.int(u8);
        e.unk5D = if (ctx.chance(2)) t5D else ctx.int(u8);
        e.unk5E = if (ctx.chance(2)) t5E else ctx.int(u8);
        e.unk1A = ctx.pick(i16, &.{ 0, 1, -1 });
        e.unk12 = ctx.pick(u8, &.{ 0xFF, 0 });
        e.unk1E = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
        psx.setBits(e, "unk8_0", @intFromBool(ctx.chance(4)));
        psx.setBits(&e.unk6C[8], "unk0_3", ctx.int(u1));
    }
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const tab = v.g.D_800F45E0();
    twin_mode = ctx.chance(2);
    const t5C = ctx.int(u8);
    const t5D = ctx.int(u8);
    const t5E = ctx.int(u8);

    for (tab) |*slot| {
        if (!twin_mode and ctx.chance(8)) {
            slot.* = null;
            continue;
        }
        const e = ctx.alloc(v.D_800F45E0_t);
        fill(ctx, e, t5C, t5D, t5E);
        slot.* = e;
    }

    const arg0 = ctx.range(u8, 0, 15);
    if (tab[arg0] == null) {
        const e = ctx.alloc(v.D_800F45E0_t);
        fill(ctx, e, t5C, t5D, t5E);
        tab[arg0] = e;
    }

    pool[0] = ctx.alloc(v.D_800F4538_t);
    pool[1] = ctx.alloc(v.D_800F4538_t);
    for (pool) |o| o.unk0.unkF = ctx.edge(u8, &.{ 0, 1, 2, 255 });

    return .{@as(c_int, arg0)};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (twin_mode) return 0;
    for (v.g.D_800F45E0().*) |slot| {
        if (slot == null) return 2;
    }
    return 1;
}

pub const stubs = struct {
    pub fn func_800A91DC(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        return @as(c_int, ctx.value(u8) % 16);
    }

    pub fn func_8009F794(ctx: *zd.Ctx, p: anytype) c_int {
        const a: ?*v.D_800F45E0_t = @ptrCast(@alignCast(p));
        if (a) |e| {
            if (ctx.value(u1) == 1) {
                e.unk12 = ctx.value(u8);
            }
        }
        if (ctx.value(u8) < 128) return -1;
        const k = ctx.value(u8) % 16;
        if (v.g.D_800F45E0()[k] == null) return -1;
        return @as(c_int, k);
    }

    pub fn func_800E68A0(ctx: *zd.Ctx, p: anytype) void {
        const a: ?*v.D_800F45E0_t = @ptrCast(@alignCast(p));
        if (a) |e| {
            if (ctx.value(u1) == 1) {
                psx.setBits(&e.unk6C[8], "unk0_4", ctx.value(u2));
            }
        }
    }

    pub fn func_800A3C00(ctx: *zd.Ctx, a: anytype, i: anytype) ?*v.D_800F4538_t {
        _ = a;
        _ = i;
        if (ctx.value(u8) < 80) return null;
        const o = pool[ctx.value(u8) % pool.len];
        if (ctx.value(u1) == 1) o.unk0.unkF = ctx.value(u8);
        if (ctx.value(u1) == 1) psx.setBits(&o.unk0, "unkA_0", ctx.value(u3));
        return o;
    }
};
