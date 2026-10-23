//! zig_diff spec: func_800A4E68 -> func_800A4E68.func_800A4E68 (3A1A0.c).
//! Inputs: one D_800F4538_t in the arena, installed in the table at a random
//! index 0..16 (index 0 a third of the time). Its flags word (the u32 at byte 8)
//! is random, with the nibble at 0xF00 drawn from the 0, 1-5 and 6+ classes, the
//! class bits 16-18, bit 21, 24-27 and 28-30 biased toward zero, and sometimes the
//! whole word zero. unk1848.unk0 covers the switch cases 0-4, 7 and default,
//! unk181A covers 0, 1 and other, unk6E4 is sometimes equal to unkC_0, and the
//! position covers 0 and the signed edges. Stubs rewrite the flags word, unk181A,
//! unk6E4 and position the way the real callees can, and return both zero and
//! nonzero from func_800A51A0.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A4E68));

pub const classes = [_][:0]const u8{
    "outer-skip",
    "arg0-zero-goto-block14",
    "arg0-zero-switch",
    "arg0-nonzero-type2",
    "arg0-nonzero-flag-path",
    "arg0-nonzero-switch",
};

fn flagsOf(a: *v.D_800F4538_t) u32 {
    return std.mem.readInt(u32, a.unk0._bits_8[0..4], .little);
}

fn setFlags(a: *v.D_800F4538_t, w: u32) void {
    std.mem.writeInt(u32, a.unk0._bits_8[0..4], w, .little);
}

/// What a callee handed the object can change: the flags word, unk181A, unk6E4.
fn touch(ctx: *zd.Ctx, p: anytype) void {
    const a: *v.D_800F4538_t = @ptrCast(@alignCast(p));
    if (ctx.value(u2) == 0) setFlags(a, ctx.value(u32));
    if (ctx.value(u2) == 0) a.unk181A = ctx.value(i16);
    if (ctx.value(u3) == 0) a.unk6E4 = ctx.value(u8);
}

pub const stubs = struct {
    pub fn func_800A4A88(ctx: *zd.Ctx, p: anytype, n: anytype) void {
        _ = n;
        touch(ctx, p);
    }

    pub fn func_800A25EC(ctx: *zd.Ctx, p: anytype) void {
        touch(ctx, p);
    }

    pub fn func_800A29A0(ctx: *zd.Ctx, p: anytype) void {
        touch(ctx, p);
    }

    pub fn func_800A2CD4(ctx: *zd.Ctx, p: anytype) void {
        touch(ctx, p);
    }

    pub fn func_800A2790(ctx: *zd.Ctx, p: anytype) void {
        touch(ctx, p);
    }

    pub fn func_800A291C(ctx: *zd.Ctx, p: anytype) void {
        touch(ctx, p);
    }

    pub fn func_800A5280(ctx: *zd.Ctx, idx: anytype, a: anytype, b: anytype, mv: anytype) c_int {
        _ = a;
        _ = b;
        _ = mv;
        // The callee reads the actor from the table; it may set class bits 16-18.
        if (v.g.D_800F4538()[@intCast(idx)]) |act| {
            if (ctx.value(u1) == 1) setFlags(act, flagsOf(act) | (@as(u32, ctx.value(u3)) << 16));
        }
        return ctx.value(c_int);
    }

    pub fn func_800AC690(ctx: *zd.Ctx, idx: anytype, p: anytype) void {
        _ = idx;
        touch(ctx, p);
    }

    pub fn func_800A51A0(ctx: *zd.Ctx, val: anytype, p: anytype) c_int {
        _ = val;
        const sv: *v.SVECTOR = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) sv.vx = ctx.value(i16);
        return @as(c_int, ctx.value(u2));
    }

    pub fn func_800A525C(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        _ = p;
    }

    pub fn func_800E6B24(ctx: *zd.Ctx, f: anytype) void {
        _ = ctx;
        _ = f;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: c_int = if (ctx.chance(3)) 0 else ctx.range(c_int, 1, 16);
    const a = ctx.alloc(v.D_800F4538_t);
    v.g.D_800F4538()[@intCast(arg0)] = a;

    // Flags word: nibble 8-11, class bits 16-18, bit 21, bits 24-27, bits 28-30.
    var w: u32 = ctx.int(u32);
    const zero_word = ctx.chance(3);
    if (zero_word) {
        w = 0;
    } else {
        w = (w & ~@as(u32, 0xF00)) | (@as(u32, ctx.pick(u4, &.{ 0, 6, 6, 7, 15, 1, 5 })) << 8);
        w = (w & ~@as(u32, 0x70000)) | (@as(u32, ctx.pick(u3, &.{ 0, 0, 0, 1, 7 })) << 16);
        w = (w & ~@as(u32, 0x200000)) | (if (ctx.chance(2)) @as(u32, 0) else @as(u32, 0x200000));
        w = (w & ~@as(u32, 0x0F000000)) | (@as(u32, ctx.pick(u4, &.{ 0, 0, 0, 1, 15 })) << 24);
        w = (w & ~@as(u32, 0x70000000)) | (@as(u32, ctx.pick(u3, &.{ 0, 0, 0, 1, 7 })) << 28);
    }
    std.mem.writeInt(u32, a.unk0._bits_8[0..4], w, .little);

    a.unk1848.unk0 = ctx.pick(u8, &.{ 0, 1, 1, 2, 3, 4, 7, 8, 255 });
    if (zero_word and ctx.chance(2)) a.unk1848.unk0 = 1;
    a.unk1848.unk4 = ctx.int(u16);
    a.unk1848.unk6 = ctx.int(i16);
    a.unk1848.unk8 = ctx.int(u32);

    a.unk181A = ctx.pick(i16, &.{ 0, 1, 1, 2, -1 });

    a.unk0.position.vx = ctx.pick(i16, &.{ 0, -1, 1, 127, -127, 128, -128, 32767, -32768 });
    a.unk0.position.vz = ctx.pick(i16, &.{ 0, -1, 1, 127, -127, 128, -128, 32767, -32768 });
    a.unk0.unkF = ctx.int(u8);

    const cc = ctx.range(u4, 0, 15);
    psx.setBits(&a.unk0, "unkC_0", cc);
    a.unk6E4 = if (ctx.chance(2)) @as(u8, cc) else ctx.pick(u8, &.{ 0, 1, 15, 16, 255 });

    return .{arg0};
}

pub fn classify(args: Args) usize {
    const arg0 = args[0];
    const a = v.g.D_800F4538()[@intCast(arg0)].?;
    const w = flagsOf(a);
    if (((w & 0xF00) != 0) and (((w >> 8) & 0xF) < 6)) return 0;
    if (arg0 == 0) {
        if (((w & 0x70000) != 0) and (a.unk181A != 0)) return 1;
        return 2;
    }
    if (a.unk1848.unk0 == 2) return 3;
    if (((w & 0x70000) != 0) or ((w & 0x0F000000) != 0) or ((w & 0x70000000) != 0)) return 4;
    return 5;
}
