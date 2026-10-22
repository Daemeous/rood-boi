//! zig_diff spec: func_800AEAE8 -> func_800AEAE8.func_800AEAE8 (44F14.c).
//! Inputs: a D_800F4538_t in the arena. Three shapes: the fixed-scale branch
//! (unk2C and unk2E both 0x1000 as one int, unk30 4096), the same int with an
//! unk30 that is not 4096 (the second && operand), and random fields with the
//! int compare false. The three offset shorts and unk183C take edge values
//! (negatives, 32767/-32768, 4096) so the signed products wrap on the i16
//! narrowing and negative divisions by 4096 are exercised. func_800B07DC is a
//! recording stub that rewrites the scratchpad offset and, sometimes, the
//! actor's unk14/unk16 (the C reads those back after the call).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AEAE8));

pub const classes = [_][:0]const u8{ "fixed-scale", "int-equal-unk30-off", "other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);

    actor.unk183C = ctx.pick(i16, &.{ 4096, 4095, 0, 1, -1, 32767, -32768, 8192 });
    psx.byteAt(i16, actor, 0x84C).* = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 4096, -4096 });
    psx.byteAt(i16, actor, 0x84E).* = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 4096, -4096 });
    psx.byteAt(i16, actor, 0x850).* = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 4096, -4096 });

    switch (ctx.range(u8, 0, 3)) {
        0 => {
            actor.unk0.unk2C = 0x1000;
            actor.unk0.unk2E = 0x1000;
            actor.unk0.unk30 = 4096;
        },
        1 => {
            actor.unk0.unk2C = 0x1000;
            actor.unk0.unk2E = 0x1000;
            actor.unk0.unk30 = ctx.pick(i16, &.{ 4095, 4097, 0, -1, -4096 });
        },
        else => {
            actor.unk0.unk2C = ctx.pick(i16, &.{ 0x1000, 0, -1, 4096, 32767 });
            actor.unk0.unk2E = ctx.pick(i16, &.{ 0x1000, 0, -1, 4096, -32768 });
            actor.unk0.unk30 = ctx.pick(i16, &.{ 4096, 0, -1, 32767 });
        },
    }
    return .{actor};
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    if (actor.unk0.unk2C == 0x1000 and actor.unk0.unk2E == 0x1000) {
        return if (actor.unk0.unk30 == 4096) 0 else 1;
    }
    return 2;
}

pub const stubs = struct {
    pub fn func_800B07DC(ctx: *zd.Ctx, p: anytype) void {
        const a: *v.D_800F4538_t = @ptrCast(@alignCast(p));
        const s: *v.actorTransformScratch = @ptrFromInt(0x1F80035C);
        s.offset.vx = ctx.value(i16);
        s.offset.vy = ctx.value(i16);
        s.offset.vz = ctx.value(i16);
        if (ctx.value(u1) == 1) a.unk0.unk14 = ctx.value(u16);
        if (ctx.value(u1) == 1) a.unk0.unk16 = ctx.value(u16);
    }
};
