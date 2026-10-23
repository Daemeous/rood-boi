//! zig_diff spec: func_800B1A68 -> func_800B1A68.func_800B1A68 (49268.c).
//! Inputs: an object in the arena with a random attachment (actorId 0-7, so
//! every branch of the attachment tests is taken), unk12 either 0xFF or a
//! holder index 0-16 whose table entry is a random D_800F4538_t, unk1A zero
//! or not (both signs), the scale block at its default 0x400040/64 or not
//! (each half of the `||`), flags with bit 1 set or clear and random bits
//! 8-11 (so the `- 4 < 5`, `>= 6` and `>= 12` tests, and `flags & 0xF00`,
//! all see both outcomes), unk64.unk0 zero or not. The camera is random. The
//! callees return 0 or 1 from func_800B3A68 and write the matrices and the
//! anchors they are given, so the C's read-backs see new values.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_49268");
const c = @import("c_49268");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B1A68));

pub const classes = [_][:0]const u8{ "holder", "ff-rotate", "low-attached", "low-plain", "rotate" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const object = ctx.alloc(v.D_800F45E0_t);
    const camera = ctx.alloc(v.MATRIX);
    const block = ctx.alloc(v.D_800F45E0_unk68);
    const holder = ctx.alloc(v.D_800F4538_t);

    psx.put(&object.unk68, block);
    psx.setBits(&object.unk6C[8], "actorId", ctx.range(u3, 0, 7));

    object.unk12 = if (ctx.chance(3)) 0xFF else ctx.range(u8, 0, 16);
    if (object.unk12 != 0xFF) v.g.D_800F4538()[object.unk12] = holder;

    object.unk1A = if (ctx.chance(3)) 0 else ctx.int(i16);
    object.unkF = ctx.int(u8);
    object.unk64.unk0 = if (ctx.chance(3)) 0 else ctx.int(u8);
    object.unk24 = ctx.int(i16);
    object.unk26 = ctx.int(i16);
    object.unk28 = ctx.int(i32);

    if (ctx.chance(2)) {
        object.unk2C = 0x40;
        object.unk2E = if (ctx.chance(4)) ctx.int(i16) else 0x40;
        object.unk30 = if (ctx.chance(4)) ctx.int(i16) else 64;
    } else {
        object.unk2C = ctx.int(i16);
        object.unk2E = ctx.int(i16);
        object.unk30 = ctx.int(i16);
    }

    var flags = ctx.int(u32);
    if (ctx.chance(2)) {
        flags |= 2;
    } else {
        flags &= ~@as(u32, 2);
    }
    psx.byteAt(u32, object, 8).* = flags;

    v.g.vs_main_nearClip().* = ctx.range(i32, -4096, 4096);

    return .{ object, camera };
}

pub fn classify(args: Args) usize {
    const object = args[0].?;
    const a = psx.getBits(&object.unk6C[8], "actorId");
    if (a >= 4) return if (object.unk12 == 0xFF) 1 else 0;
    if (a < 2) return if (object.unk1A != 0) 2 else 3;
    return 4;
}

fn fillMatrix(ctx: *zd.Ctx, m: *v.MATRIX) void {
    m.m[0][0] = ctx.value(i16);
    m.m[0][1] = ctx.value(i16);
    m.m[1][1] = ctx.value(i16);
    m.m[2][2] = ctx.value(i16);
    m.t[0] = ctx.value(i32);
    m.t[1] = ctx.value(i32);
    m.t[2] = ctx.value(i32);
}

pub const stubs = struct {
    pub fn func_800A190C(ctx: *zd.Ctx, id: anytype, slot: anytype, out: anytype, n: anytype) c_int {
        _ = id;
        _ = slot;
        _ = n;
        const a: *v.SVECTOR = @ptrCast(@alignCast(out));
        a.vx = ctx.value(i16);
        a.vy = ctx.value(i16);
        a.vz = ctx.value(i16);
        return ctx.value(c_int);
    }

    pub fn func_800B17F0(ctx: *zd.Ctx, r: anytype, m: anytype) void {
        _ = r;
        fillMatrix(ctx, @ptrCast(@alignCast(m)));
    }

    pub fn func_800B196C(ctx: *zd.Ctx, a: anytype, b: anytype) void {
        _ = a;
        fillMatrix(ctx, @ptrCast(@alignCast(b)));
    }

    pub fn func_800B396C(ctx: *zd.Ctx, m: anytype, vec: anytype) void {
        _ = vec;
        fillMatrix(ctx, @ptrCast(@alignCast(m)));
    }

    pub fn RotMatrixYXZ_gte(ctx: *zd.Ctx, r: anytype, m: anytype) [*c]c.MATRIX {
        _ = r;
        fillMatrix(ctx, @ptrCast(@alignCast(m)));
        return null;
    }

    // The extern declares three parameters; the C call passes two (the
    // in-place form), so the third is ignored.
    pub fn CompMatrixLV(ctx: *zd.Ctx, m0: anytype, m1: anytype, m2: anytype) [*c]c.MATRIX {
        _ = m1;
        _ = m2;
        fillMatrix(ctx, @ptrCast(@alignCast(m0)));
        return null;
    }

    pub fn func_800B3A68(ctx: *zd.Ctx, obj: anytype, m: anytype, k: anytype, model: anytype) c_int {
        _ = obj;
        _ = m;
        _ = k;
        _ = model;
        if (ctx.value(u2) == 0) return 0;
        return 1;
    }
};
