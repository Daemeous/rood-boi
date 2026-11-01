//! zig_diff spec: func_800B002C -> func_800B002C.func_800B002C (44F14.c).
//! Inputs: an actor with its scale at the 0x1000 identity or not, the
//! bone offset at 0x84C and unk183C any i16; arg1 0 or not; the scale,
//! rotation and movement counters (unk5C8, unk18, unk1A) 0, 1, small or
//! any, the deltas any (the divisions' signs); unkA_3 0-3 when the
//! movement ends; the word at +8 with its 0x3F0000 bits, sign bit and the
//! 0x400000 / 0x200000 animation classes; animation ids at each range's
//! edges (+100 too); the look target: none (-2, with unk14/unk16 zero or
//! not), fixed angles (-4, -3, at the 0x800 wrap and the 800 clamp), a
//! point (-1) or an actor 0-16; the head limit (unk17F4.pad) 0, 800 or
//! small. func_800BEB00 returns 4 or not.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_44F14");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B002C));

pub const classes = [_][:0]const u8{ "no-target", "angles", "point", "actor" };

pub const stubs = struct {
    pub fn func_800BEB00(ctx: *zd.Ctx) c_short {
        return if (ctx.value(u2) == 0) 4 else ctx.value(c_short);
    }
};

const angle_edges = [_]i16{ 0, 799, 800, 801, -799, -800, -801, 0x7FF, 0x800, -0x800, -0x801, 0xFFF, 100 };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = ctx.alloc(v.D_800F4538_t);
    const body = &a.unk0;
    if (ctx.chance(3)) {
        body.unk2C = 0x1000;
        body.unk2E = 0x1000;
        body.unk30 = if (ctx.chance(4)) 4095 else 4096;
    }
    a.unk5C8 = ctx.pick(u16, &.{ 0, 1, 2, 3 });
    body.unk18 = ctx.pick(u16, &.{ 0, 1, 2, 3 });
    body.unk1A = ctx.pick(u16, &.{ 0, 1, 1, 2 });
    psx.setBits(body, "unkA_3", ctx.int(u2));
    if (ctx.chance(3)) psx.setBits(body, "unkC_0", 0);
    var w: u32 = ctx.int(u32);
    if (ctx.chance(2)) w &= ~@as(u32, 0x3F0000);
    if (ctx.chance(2)) w &= ~@as(u32, 0x80000000);
    switch (ctx.range(u8, 0, 2)) {
        0 => w = (w | 0x400000),
        1 => w = (w & ~@as(u32, 0x400000)) | 0x200000,
        else => w &= ~@as(u32, 0x600000),
    }
    if (ctx.chance(16)) w = 0;
    psx.byteAt(u32, a, 8).* = w;
    const id: u8 = ctx.edge(u8, &.{ 0, 1, 6, 7, 8, 13, 14, 17, 18, 19, 30, 31, 34, 35, 38, 39, 99, 100, 101, 131, 255 });
    a.animationId = id;

    const kind = ctx.range(u8, 0, 3);
    a.unk17FC = switch (kind) {
        0 => -2,
        1 => if (ctx.chance(2)) -4 else -3,
        2 => -1,
        else => ctx.range(i8, 0, 16),
    };
    if (kind == 0 and ctx.chance(2)) {
        body.unk14 = 0;
        body.unk16 = 0;
    } else if (ctx.chance(2)) {
        body.unk14 = 0;
        body.unk16 = 0;
    }
    a.unk17F4.vx = ctx.pick(i16, &angle_edges);
    a.unk17F4.vy = ctx.pick(i16, &angle_edges);
    a.unk17F4.pad = ctx.pick(i16, &.{ 0, 800, 799, 100, -1 });
    if (kind == 1 and a.unk17FC == -3) {
        body.pitch = if (ctx.chance(2)) 0 else ctx.int(i16);
        body.facing = if (ctx.chance(2)) 0 else ctx.int(i16);
    }
    var k: usize = 0;
    while (k < 17) : (k += 1) {
        v.g.D_800F4538()[k] = if (ctx.chance(3)) a else ctx.alloc(v.D_800F4538_t);
    }
    const arg1: c_int = if (ctx.chance(3)) ctx.int(c_int) | 1 else 0;
    return .{ a, arg1 };
}

pub fn classify(args: Args) usize {
    const t = args[0].?.unk17FC;
    if (t == -2) return 0;
    if (t == -4 or t == -3) return 1;
    return if (t == -1) 2 else 3;
}
