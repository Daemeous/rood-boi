//! zig_diff spec: func_800AF6E8 -> func_800AF6E8.func_800AF6E8 (44F14.c).
//! Inputs: a random D_800F4538_t in the arena whose unk5CC (the step counter)
//! covers 0, 1, other values with and without the sign bit, whose unk5B2 is 1
//! or not, and whose unk5CD is biased to equal the stepped counter, so both
//! `>=` tests see equal sides often. The callees are stubs that write the
//! fields the C reads back (nBones, unk5B2, unk5CD, unk5CC, the unkC54 bit
//! and the copied block) and return nothing; vs_main_memcpy really copies.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AF6E8));

pub const classes = [_][:0]const u8{ "idle", "first-chunk", "no-step", "finished", "continue" };

fn view(comptime T: type, p: anytype) *T {
    return @ptrCast(@alignCast(@as(?*anyopaque, p).?));
}

pub const stubs = struct {
    pub fn func_800AFA28(ctx: *zd.Ctx, p: anytype, q: anytype, k: anytype) void {
        _ = k;
        const a = view(v.D_800F4538_t, p);
        const cc = view(v.D_800F4538_unkC54, q);
        if (ctx.value(u1) == 1) cc.unk540 = ctx.value(u16);
        if (ctx.value(u2) == 0) a.unk0.nBones = ctx.value(u8);
        if (ctx.value(u2) == 0) a.unk5B2 = ctx.value(u8);
        if (ctx.value(u1) == 1) psx.setBits(cc, "unk548_16", ctx.value(i1));
    }

    pub fn func_800AF844(ctx: *zd.Ctx, p: anytype, q: anytype, n: anytype) void {
        _ = q;
        _ = n;
        if (ctx.value(u1) == 1) view(v.SVECTOR, p).vy = ctx.value(i16);
    }

    pub fn func_800B147C(ctx: *zd.Ctx, p: anytype, q: anytype, n: anytype, cc: anytype, cd: anytype) void {
        _ = n;
        _ = cc;
        _ = cd;
        const dst = view(v.D_800F4538_unkC54, p);
        if (ctx.value(u2) == 0) dst.unk542 = ctx.value(u16);
        if (ctx.value(u1) == 1) psx.setBits(view(v.D_800F4538_unkC54, q), "unk548_16", ctx.value(i1));
    }

    pub fn func_800B002C(ctx: *zd.Ctx, p: anytype, k: anytype) void {
        _ = k;
        const a = view(v.D_800F4538_t, p);
        if (ctx.value(u2) == 0) a.unk5CD = ctx.value(u8);
        if (ctx.value(u3) == 0) a.unk5CC = ctx.value(u8);
    }

    pub fn vs_main_memcpy(ctx: *zd.Ctx, dest: anytype, src: anytype, sz: anytype) void {
        _ = ctx;
        const n: usize = @intCast(sz);
        const d: [*]u8 = @ptrCast(@alignCast(@as(?*anyopaque, dest).?));
        const s: [*]const u8 = @ptrCast(@alignCast(@as(?*const anyopaque, src).?));
        @memcpy(d[0..n], s[0..n]);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = ctx.alloc(v.D_800F4538_t);
    a.unk5CC = ctx.pick(u8, &.{ 0, 1, 2, 3, 0x7F, 0x80, 0xFE, 0xFF });
    a.unk5B2 = ctx.pick(u8, &.{ 0, 1, 2, 0x7F, 0x80, 0xFF });
    const next: u8 = a.unk5CC +% 0xFF;
    const newcc: u8 = a.unk5B2 +% next;
    a.unk5CD = ctx.pick(u8, &.{ 0, 1, 2, 0x7F, 0x80, 0xFF });
    if (ctx.chance(3)) a.unk5CD = newcc;
    if (ctx.chance(4)) a.unk5CD = newcc +% 1;
    a.unk0.nBones = ctx.int(u8);
    return .{a};
}

pub fn classify(args: Args) usize {
    const a = args[0].?;
    const cc = a.unk5CC;
    if (cc == 0) return 0;
    if (cc == 1) return 1;
    if (a.unk5B2 == 1) return 2;
    const next: u8 = cc +% 0xFF;
    const newcc: u8 = a.unk5B2 +% next;
    return if (newcc >= a.unk5CD) 3 else 4;
}
