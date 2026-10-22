//! zig_diff spec: func_800AE4FC -> func_800AE4FC.func_800AE4FC (44F14.c).
//! Inputs: arg0 is a fresh D_800F4538_unk0 in the arena. Its position.vy sits
//! on the 3000 (0xBB8) cutoff often, so the comparison sees equal sides, and
//! its unkF picks one of 17 table slots that prepare fills with a fresh actor.
//! The actor's unk17E4 bytes cover the flag-2 test, the zero and nonzero
//! unk1/unk2 overrides, and the 32-bit word at unk0.nQuads + 4 bytes (bits
//! 8..21 equal to 0x8000 half the time). unk68 points at a block whose
//! akaoOffset is a guest address. The stubs: vs_main_computeSfxPan returns a
//! temp whose low half is zero half the time (sp[1] == 0); func_800A1280
//! rewrites the actor's unk6FC, which the C re-reads; rand() is the harness
//! default.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AE4FC));

pub const classes = [_][:0]const u8{ "early-return", "flag-two", "bits-equal", "unk1-override", "unk2-override", "plain" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.alloc(v.D_800F4538_unk0);
    const actor = ctx.alloc(v.D_800F4538_t);
    const block = ctx.alloc(v.D_800F4538_unk68);
    const akao = ctx.alloc(u32);

    psx.put(&arg0.unk68, block);
    psx.put(&block.akaoOffset, akao);

    arg0.position.vy = ctx.pick(i16, &.{ 3000, 2999, 3001, -1, 0, 32767, -32768 });
    arg0.unkF = ctx.range(u8, 0, 16);
    v.g.D_800F4538()[arg0.unkF] = actor;

    const flag = &actor.unk17E4;
    flag.unk0 = if (ctx.chance(3)) 2 else ctx.pick(u8, &.{ 0, 1, 3, 0xFF });
    flag.unk1 = if (ctx.chance(2)) 0 else ctx.pick(u8, &.{ 1, 0x7F, 0x80, 0xFF });
    flag.unk2 = if (ctx.chance(2)) 0 else ctx.pick(u8, &.{ 1, 0x7F, 0x80, 0xFF });

    // The word the C reads at unk0.nQuads + 4 bytes (offset 8 of unk0).
    var word = ctx.int(u32);
    if (ctx.chance(2)) word = (word & ~@as(u32, 0x3F9F00)) | 0x8000;
    std.mem.writeInt(u32, actor.unk0._bits_8[0..4], word, .little);

    const t = ctx.pick(i32, &.{ 8, 4, 0, 1, 7, 9, 0x180, -1 });
    return .{ arg0, t };
}

pub fn classify(args: Args) usize {
    const arg0 = args[0].?;
    if (arg0.position.vy >= 0xBB8) return 0;
    const actor = v.g.D_800F4538()[arg0.unkF].?;
    if (actor.unk17E4.unk0 == 2) return 1;
    const word = std.mem.readInt(u32, actor.unk0._bits_8[0..4], .little);
    if ((word & 0x3F9F00) == 0x8000) return 2;
    if (actor.unk17E4.unk1 != 0) return 3;
    if (actor.unk17E4.unk2 != 0) return 4;
    return 5;
}

pub const stubs = struct {
    pub fn vs_main_computeSfxPan(ctx: *zd.Ctx, coords: anytype, z: anytype) c_int {
        _ = coords;
        _ = z;
        const r = ctx.value(u32);
        // Half the time the low half is zero: the C returns at sp[1] == 0.
        if (ctx.value(u1) == 1) return @bitCast(r & 0xFFFF_0000);
        return @bitCast(r);
    }

    pub fn func_800A1280(ctx: *zd.Ctx, id: anytype, mask: anytype, sv: anytype, flags: anytype) c_int {
        _ = id;
        _ = mask;
        _ = flags;
        const p: *v.SVECTOR = @ptrCast(@alignCast(sv));
        if (ctx.value(u1) == 1) p.vx = ctx.value(i16);
        if (ctx.value(u1) == 1) p.vy = ctx.value(i16);
        p.vz = ctx.value(i16);
        return ctx.value(c_int);
    }
};
