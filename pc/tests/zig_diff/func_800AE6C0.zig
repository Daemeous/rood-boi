//! zig_diff spec: func_800AE6C0 -> func_800AE6C0.func_800AE6C0 (44F14.c).
//! Inputs: an arg0 object whose unkF picks one of the 17 entries of
//! D_800F4538 (set by prepare to an arena object, sometimes arg0 itself).
//! Its unk17E4.unk0 is 2 (the sfx-less branch) or not; its six flag bits
//! each trigger the func_800A1280 call alone, all clear, or all random;
//! unk17E4.unk1/unk2 are zero or not (unk2 is 1 sometimes, for the `> 1`
//! slip). The stubs rewrite the SVECTOR, and unk1/unk2 of the table entry,
//! so the C must re-read them after the call. computeSfxPan returns a word
//! whose low half is zero a quarter of the time. arg1 and arg2 take negative
//! values, and arg2 is sometimes -1 (the `!= 0` vs `> 0` slip).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AE6C0));

pub const classes = [_][:0]const u8{ "unk0-is-2", "no-sfx-flag", "sfx-flag", "aliased-temp" };

pub const stubs = struct {
    /// Writes the SVECTOR it is given, and sometimes the table entry's
    /// unk17E4.unk1/unk2 (both read back by the C after the call).
    /// Returns a c_int, as the C prototype declares.
    pub fn func_800A1280(ctx: *zd.Ctx, idx: anytype, sfx: anytype, vec: anytype, zero: anytype) c_int {
        _ = sfx;
        _ = zero;
        const sv: *v.SVECTOR = @ptrCast(@alignCast(vec));
        if (ctx.value(u2) != 0) sv.vx = ctx.value(i16);
        if (ctx.value(u2) != 0) sv.vy = ctx.value(i16);
        if (ctx.value(u2) != 0) sv.vz = ctx.value(i16);
        const table = v.g.D_800F4538();
        if (table[@intCast(idx)]) |t| {
            if (ctx.value(u2) == 0) t.unk17E4.unk1 = ctx.value(u8);
            if (ctx.value(u2) == 0) t.unk17E4.unk2 = ctx.value(u8);
        }
        return ctx.value(c_int);
    }

    /// Returns a word whose low half is zero a quarter of the time (sp10[1]
    /// stays 0 unless unk2 overrides it), otherwise any 32-bit value.
    pub fn vs_main_computeSfxPan(ctx: *zd.Ctx, coords: anytype, z: anytype) c_int {
        _ = coords;
        _ = z;
        if (ctx.value(u8) < 64) return @bitCast(@as(u32, ctx.value(u16)) << 16);
        return ctx.value(i32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    const temp: *v.D_800F4538_t = if (ctx.chance(8)) obj else ctx.alloc(v.D_800F4538_t);
    const idx = ctx.range(u8, 0, 16);
    obj.unk0.unkF = idx;
    v.g.D_800F4538()[idx] = temp;

    temp.unk17E4.unk0 = if (ctx.chance(3)) 2 else ctx.pick(u8, &.{ 0, 1, 3, 255 });
    temp.unk17E4.unk1 = if (ctx.chance(2)) 0 else ctx.int(u8);
    temp.unk17E4.unk2 = ctx.pick(u8, &.{ 0, 1, 2, 255 });

    // trig 0..5: only that one flag is set (the others clear);
    // trig 6: all clear (no call); trig 7: all random.
    const trig = ctx.range(u8, 0, 7);
    psx.setBits(&temp.unk0, "unk9_0", if (trig == 0) ctx.range(u4, 1, 15) else if (trig == 7) ctx.int(u4) else 0);
    psx.setBits(&temp.unk0, "unk9_4", if (trig == 1) 1 else if (trig == 7) ctx.int(u1) else 0);
    psx.setBits(&temp.unk0, "unk9_7", if (trig == 2) 0 else if (trig == 7) ctx.int(u1) else 1);
    psx.setBits(&temp.unk0, "unkA_0", if (trig == 3) ctx.range(u3, 1, 7) else if (trig == 7) ctx.int(u3) else 0);
    psx.setBits(&temp.unk0, "unkA_3", if (trig == 4) ctx.range(u2, 1, 3) else if (trig == 7) ctx.int(u2) else 0);
    psx.setBits(&temp.unk0, "unkA_5", if (trig == 5) 1 else if (trig == 7) ctx.int(u1) else 0);

    const arg1 = ctx.pick(i32, &.{ 0, 1, -1, 0x7FFF_FFFF, -0x8000_0000 });
    const arg2 = ctx.pick(i32, &.{ 0, 0, 1, -1, 0x7FFF_FFFF });
    return .{ obj, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const obj = args[0].?;
    const temp = v.g.D_800F4538()[obj.unk0.unkF].?;
    if (temp.unk17E4.unk0 == 2) return 0;
    if (temp == obj) return 3;
    const u = &temp.unk0;
    const flagged = psx.getBits(u, "unk9_0") != 0 or
        psx.getBits(u, "unk9_4") != 0 or
        psx.getBits(u, "unk9_7") == 0 or
        psx.getBits(u, "unkA_0") != 0 or
        psx.getBits(u, "unkA_3") != 0 or
        psx.getBits(u, "unkA_5") != 0;
    return if (flagged) 2 else 1;
}
