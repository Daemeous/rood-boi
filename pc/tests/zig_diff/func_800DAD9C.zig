//! zig_diff spec: func_800DAD9C -> func_800DAD9C.func_800DAD9C (71618.c).
//! Inputs: an actionDispatchState in the arena whose 16 entries are fresh
//! arena objects and whose count runs from -2 to 16 (a count of 16 is the
//! last valid entry). The fallback's flags4 bits 5-12 are 0, 0x80 (the sign
//! bit of that field), 1 or random. The registry (D_800F58BC) is always
//! valid: each of its four slots is null or one of four pool actors with an
//! id in 0-15. All 16 entries of D_800F45E0 are arena models whose id is 0,
//! 1, 0x80, 0xFF or random and whose status is 0, 1, -1 or random. The
//! func_800DAC80 stub returns nonzero one call in 16 and writes the count, a
//! registry slot, a model status and the fallback's flagsC, which the C
//! reads back.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_71618");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DAD9C));

// Set by prepare, read by the stub and classify (the same in both runs).
var cur: *v.actionDispatchState = undefined;
var reg: *v.actionDispatchRegistry = undefined;
var pool: [4]*v.actionDispatchActor = undefined;
var models: [16]*v.actionDispatchModel = undefined;

pub const stubs = struct {
    pub fn func_800DAC80(ctx: *zd.Ctx, context: anytype, action: anytype) c_int {
        _ = context;
        _ = action;
        if (ctx.value(u8) < 24) cur.count = @as(i32, ctx.value(u8) % 17);
        if (ctx.value(u8) < 32) {
            const link: ?*v.actionDispatchActor = if (ctx.value(u1) == 1) null else pool[ctx.value(u8) % 4];
            psx.put(&reg.registered[ctx.value(u8) % 4], link);
        }
        if (ctx.value(u8) < 32) models[ctx.value(u8) % 16].status = ctx.value(i16);
        if (ctx.value(u8) < 32) cur.fallback.flagsC = ctx.value(u32);
        if (ctx.value(u8) < 16) return 1;
        return 0;
    }
};

pub const classes = [_][:0]const u8{
    "count-le-0",
    "fallback-field-zero",
    "no-actor",
    "actor-dead-model",
    "actor-live-model",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const st = ctx.alloc(v.actionDispatchState);
    cur = st;

    st.count = if (ctx.chance(4)) ctx.range(i32, -2, 0) else ctx.range(i32, 1, 16);
    for (&st.entries) |*slot| {
        psx.put(slot, ctx.alloc(v.actionDispatchEntry));
    }

    // Fallback: flags4 bits 5-12 (the field the C tests) are 0, 0x80, 1 or random.
    const f8 = ctx.pick(u8, &.{ 0, 0x80, 1, 0xFF });
    st.fallback.flags4 = (ctx.int(u32) & ~@as(u32, 0x1FE0)) | (@as(u32, f8) << 5);
    st.fallback.flagsC = ctx.int(u32);

    // Registry: always valid, so the C may read it.
    reg = ctx.alloc(v.actionDispatchRegistry);
    const rp: *v.D_800F58BC_t = @ptrCast(@alignCast(reg));
    v.g.D_800F58BC().* = rp;

    for (0..4) |k| {
        const a = ctx.alloc(v.actionDispatchActor);
        a.id = ctx.range(i32, 0, 15);
        pool[k] = a;
    }

    // All 16 model slots point at valid models, so any index the C reads is safe.
    for (0..16) |k| {
        const m = ctx.alloc(v.actionDispatchModel);
        m.id = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
        m.status = ctx.pick(i16, &.{ 0, 0, 1, -1 });
        models[k] = m;
        const gp: *v.D_800F45E0_t = @ptrCast(@alignCast(m));
        v.g.D_800F45E0()[k] = gp;
    }

    const none = ctx.chance(3);
    for (&reg.registered) |*slot| {
        const empty: ?*v.actionDispatchActor = null;
        if (none or ctx.chance(3)) {
            psx.put(slot, empty);
        } else {
            psx.put(slot, pool[ctx.range(usize, 0, 3)]);
        }
    }

    return .{st};
}

pub fn classify(args: Args) usize {
    const st = args[0].?;
    if (st.count <= 0) return 0;
    if (((st.fallback.flags4 >> 5) & 255) == 0) return 1;
    var any = false;
    for (reg.registered) |slot| {
        const a = psx.get(slot) orelse continue;
        any = true;
        const m = models[@intCast(a.id)];
        if (m.id != 0 and m.status == 0) return 4;
    }
    return if (any) 3 else 2;
}
