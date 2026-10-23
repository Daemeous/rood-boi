//! zig_diff spec: func_800A4494 -> func_800A4494.func_800A4494 (3A1A0.c).
//! Inputs: a contact id (mostly 0-15, sometimes outside the 4-bit unkC_0
//! range), a point, and 17 table slots, each null, skipped, a contact match
//! with any unkA_3, or a window candidate. Per run a mode picks: no actors,
//! all actors far from the point, window actors, sparse contact matches or
//! dense ones. Window positions sit on each axis's edges (equal sides), just
//! inside and just outside, and point.vy hits position.vy-288 and position.vy
//! exactly. func_800A46A4 returns zero or any int and nudges the actor's x
//! (the C reads it back); func_800A4A88 sometimes writes unk6E6.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A4494));

pub const classes = [_][:0]const u8{
    "no-live-actor",
    "contact-ready",
    "contact-not-ready",
    "window-goto",
    "window-return",
    "outside",
};

pub const stubs = struct {
    pub fn func_800A46A4(ctx: *zd.Ctx, actor: anytype) c_int {
        const a: *v.D_800F4538_t = @ptrCast(@alignCast(actor));
        if (ctx.value(u2) != 0) a.unk0.position.vx = ctx.value(i16);
        if (ctx.value(u1) == 0) return 0;
        return ctx.value(c_int);
    }

    pub fn func_800A4A88(ctx: *zd.Ctx, actor: anytype, mode: anytype) void {
        _ = mode;
        const a: *v.D_800F4538_t = @ptrCast(@alignCast(actor));
        if (ctx.value(u1) == 1) a.unk6E6 = ctx.value(u16);
    }
};

/// Wraps a signed 32-bit value to i16 (modulo 2^16) without a signed truncate.
fn wrap16(x: i32) i16 {
    const u: u32 = @bitCast(x);
    return @bitCast(@as(u16, @truncate(u)));
}

fn axisPos(ctx: *zd.Ctx, p: i16, radius: u16) i16 {
    const pv: i32 = p;
    const r: i32 = radius;
    // d = 0 puts the position exactly on the window edge; d < 0 / d > 0 move it inside / outside.
    const d: i32 = if (ctx.chance(2)) 0 else ctx.range(i32, -200, 200);
    const base: i32 = if (ctx.chance(2)) pv + 64 + r else pv - 64 - r;
    return wrap16(base - d);
}

fn farPos(p: i16) i16 {
    const off: i32 = if (p < 0) 3000 else -3000;
    return wrap16(@as(i32, p) + off);
}

fn yPos(ctx: *zd.Ctx, pvy: i16) i16 {
    const pv: i32 = pvy;
    // e = 0: point.vy == y - 288 (equal); e = 288: point.vy == y (equal).
    const e: i32 = if (ctx.chance(2))
        (if (ctx.chance(2)) 0 else 288)
    else
        ctx.range(i32, -5, 300);
    return wrap16(pv + 288 - e);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const point = ctx.alloc(v.SVECTOR);
    point.vx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 64, -64 });
    point.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 288, -288 });
    point.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 64, -64 });
    point.pad = ctx.int(i16);

    const contact: c_int = if (ctx.chance(6))
        ctx.pick(c_int, &.{ 16, -1, 0x10000, -16, 0x7FFF_FFFF })
    else
        ctx.range(c_int, 0, 15);

    const mode = ctx.range(u8, 0, 19);
    const far = mode >= 1 and mode <= 4;
    const matchRate: u32 = if (mode >= 14) 2 else if (mode >= 9) 8 else 0;
    const want: u4 = @truncate(@as(u32, @bitCast(contact)));

    const tbl = v.g.D_800F4538();
    var i: usize = 0;
    while (i < 17) : (i += 1) {
        if (mode == 0 or ctx.chance(5)) {
            tbl[i] = null;
            continue;
        }
        const a = ctx.alloc(v.D_800F4538_t);
        tbl[i] = a;
        psx.setBits(&a.unk0, "skip", if (ctx.chance(6)) 1 else 0);

        const match = matchRate != 0 and ctx.chance(matchRate);
        var nb = ctx.int(u4);
        if (nb == want) nb +%= 1;
        psx.setBits(&a.unk0, "unkC_0", if (match) want else nb);
        psx.setBits(&a.unk0, "unkA_3", ctx.pick(u2, &.{ 2, 2, 0, 1, 3 }));

        const radius: u16 = if (far) 0 else ctx.pick(u16, &.{ 0, 1, 64, 288, 0xFFFF });
        a.unk63C = radius;
        if (far) {
            a.unk0.position.vx = farPos(point.vx);
            a.unk0.position.vz = farPos(point.vz);
        } else {
            a.unk0.position.vx = axisPos(ctx, point.vx, radius);
            a.unk0.position.vz = axisPos(ctx, point.vz, radius);
        }
        a.unk0.position.vy = yPos(ctx, point.vy);

        std.mem.writeInt(i16, a.unk17EC[0..2], ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 }), .little);
        std.mem.writeInt(i16, a.unk17EC[4..6], ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 }), .little);
    }

    return .{ contact, point };
}

fn inWindow(a: *v.D_800F4538_t, p: *v.SVECTOR) bool {
    const radius: i32 = a.unk63C;
    const pos = a.unk0.position;
    const x: i32 = p.vx;
    const z: i32 = p.vz;
    const vy: i32 = p.vy;
    const px: i32 = pos.vx;
    const pz: i32 = pos.vz;
    const y: i32 = pos.vy;
    if (x + 64 < px - radius or x - 64 > px + radius) return false;
    if (z + 64 < pz - radius or z - 64 > pz + radius) return false;
    if (vy < y - 288 or vy >= y) return false;
    return true;
}

pub fn classify(args: Args) usize {
    const contact: c_int = args[0];
    const point = args[1].?;
    const tbl = v.g.D_800F4538();
    var live = false;
    var ready = false;
    var notReady = false;
    var toGoto = false;
    var returns = false;
    for (tbl.*) |maybe| {
        const a = maybe orelse continue;
        if (psx.getBits(&a.unk0, "skip") != 0) continue;
        live = true;
        if (@as(c_int, psx.getBits(&a.unk0, "unkC_0")) == contact) {
            if (psx.getBits(&a.unk0, "unkA_3") == 2) {
                ready = true;
            } else {
                notReady = true;
            }
        } else if (inWindow(a, point)) {
            if (psx.getBits(&a.unk0, "unkA_3") == 2) {
                toGoto = true;
            } else {
                returns = true;
            }
        }
    }
    if (!live) return 0;
    if (ready) return 1;
    if (notReady) return 2;
    if (toGoto) return 3;
    if (returns) return 4;
    return 5;
}
