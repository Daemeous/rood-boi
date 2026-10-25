//! zig_diff spec: func_800A1280 -> func_800A1280.func_800A1280 (30DB0.c).
//! Inputs: arg1 is 0xFB about half the time (the GTE path) and otherwise a
//! selector the C passes on. On the GTE path arg0 is 0..16, the D_800F4538
//! entry is null or a valid object (its unkB_0 zero or nonzero), the
//! D_800F45E0 fallback is null or valid, and the position, unk646, unk1C,
//! unk1E and unk20 fields take i16/u16 edge values. Off that path arg0 is any
//! int. The stubs of RotTransPers (which writes sp10 for the high/low halves),
//! func_800A152C and func_800A141C draw their results with ctx.value, so
//! zero, negative and positive returns all occur.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A1280));

pub const classes = [_][:0]const u8{
    "fb-no-entry",
    "fb-fallback-entry",
    "fb-actor-flagged",
    "fb-actor",
    "other-selector",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const fb = ctx.chance(2);
    const arg1: c_int = if (fb) 0xFB else ctx.pick(c_int, &.{ 0, 0xFA, 0xFC, -1 });
    var arg0: c_int = ctx.int(c_int);
    var mode: u8 = 0;
    if (fb) {
        arg0 = ctx.range(c_int, 0, 16);
        mode = ctx.range(u8, 0, 3);
        // D_800F45E0 has 16 entries: arg0 == 16 needs the actor entry.
        if (arg0 == 16 and mode < 2) mode = 2;
    }

    const t4 = v.g.D_800F4538();
    const t45 = v.g.D_800F45E0();
    for (t4) |*e| e.* = null;
    for (t45) |*e| e.* = null;
    // pc_gte_anchored is incremented around the call: keep it away from overflow.
    v.g.pc_gte_anchored().* = ctx.range(i32, 0, 1000);

    const arg2 = ctx.alloc(v.SVECTOR);

    if (fb) {
        const idx: usize = @intCast(arg0);
        switch (mode) {
            0 => {},
            1 => {
                const e = ctx.alloc(v.D_800F45E0_t);
                e.unk1C = ctx.pick(i16, &.{ 0, -1, 32767, -32768, 192 });
                e.unk1E = ctx.pick(i16, &.{ 0, -1, 32767, -32768, 192 });
                e.unk20 = ctx.pick(i16, &.{ 0, -1, 32767, -32768 });
                t45[idx] = e;
            },
            else => {
                const a = ctx.alloc(v.D_800F4538_t);
                a.unk0.position.vx = ctx.pick(i16, &.{ 0, -1, 32767, -32768 });
                a.unk0.position.vy = ctx.pick(i16, &.{ 0, -1, 32767, -32768, 192 });
                a.unk0.position.vz = ctx.pick(i16, &.{ 0, -1, 32767, -32768 });
                a.unk646 = ctx.pick(u16, &.{ 0, 1, 0x8000, 0xFFFF });
                psx.setBits(&a.unk0, "unkB_0", if (mode == 3) ctx.range(u4, 1, 15) else 0);
                t4[idx] = a;
            },
        }
    }

    const arg3 = ctx.int(c_int);
    return .{ arg0, arg1, arg2, arg3 };
}

pub fn classify(args: Args) usize {
    if (args[1] != 0xFB) return 4;
    const idx: usize = @intCast(args[0]);
    const t4 = v.g.D_800F4538();
    if (t4[idx]) |a| {
        return if (psx.getBits(&a.unk0, "unkB_0") != 0) 2 else 3;
    }
    const t45 = v.g.D_800F45E0();
    return if (t45[idx] != null) 1 else 0;
}

pub const stubs = struct {
    pub fn SetRotMatrix(ctx: *zd.Ctx, m: anytype) void {
        _ = ctx;
        _ = m;
    }

    pub fn SetTransMatrix(ctx: *zd.Ctx, m: anytype) void {
        _ = ctx;
        _ = m;
    }

    // The real callee writes its three out-parameters; the C reads sp10 back
    // (both halves) and the return value goes to arg2->vz.
    pub fn RotTransPers(ctx: *zd.Ctx, v0: anytype, sxy: anytype, p: anytype, flag: anytype) c_int {
        _ = v0;
        const s: *c_int = @ptrCast(@alignCast(sxy));
        const q: *c_int = @ptrCast(@alignCast(p));
        const f: *c_int = @ptrCast(@alignCast(flag));
        s.* = ctx.value(c_int);
        q.* = ctx.value(c_int);
        f.* = ctx.value(c_int);
        return ctx.value(c_int);
    }

    pub fn func_800A152C(ctx: *zd.Ctx, a0: anytype, a1: anytype, a2: anytype) c_int {
        _ = a0;
        _ = a1;
        _ = a2;
        if (ctx.value(u8) < 24) return 0; // the C's `< 0` sees equal sides
        return ctx.value(c_int);
    }

    pub fn func_800A141C(ctx: *zd.Ctx, a0: anytype, a1: anytype, a2: anytype, a3: anytype) c_int {
        _ = a0;
        _ = a1;
        _ = a2;
        _ = a3;
        if (ctx.value(u8) < 24) return 0;
        return ctx.value(c_int);
    }
};
