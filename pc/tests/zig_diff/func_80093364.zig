//! zig_diff spec: func_80093364 -> func_80093364.func_80093364 (2842C.c).
//! Inputs: one func_80092F74_t in the arena. Positions, velocities, accelerations
//! and vector components are random or drawn from edges (0, +-1, 16, the i16 and
//! i32 extremes, 0x0800 and 0x0FFF masks), so the adds wrap and the &= 0xFFF
//! mask sees bit 11 set and clear. The decay factors unk2A and unk32 are zero
//! or not, with both signs, which covers both guards in all four combinations.
//! func_80092EDC is a recording stub that sometimes rewrites unkC and vx.

const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80093364));

pub const classes = [_][:0]const u8{ "both-zero", "2A-only", "32-only", "both-set" };

pub const stubs = struct {
    pub fn func_80092EDC(ctx: *zd.Ctx, arg0: anytype) void {
        const a: *v.func_80092F74_t = @ptrCast(@alignCast(arg0));
        if (ctx.value(u1) == 1) a.unkC = ctx.value(i32);
        if (ctx.value(u1) == 1) a.unk1C.vx = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_80092F74_t);
    const word = [_]i16{ 0, 1, -1, 16, -16, 0x7FFF, -0x8000, 0x0FFF, 0x0800 };
    const wide = [_]i32{ 0, -1, 0x7FFF_FFFF, -0x8000_0000 };

    obj.unkC = ctx.pick(i32, &wide);
    obj.unk10 = ctx.pick(i32, &wide);
    obj.unk14 = ctx.pick(i32, &wide);

    obj.unk24 = ctx.pick(i16, &word);
    obj.unk26 = ctx.pick(i16, &word);
    obj.unk28 = ctx.pick(i16, &word);

    obj.unk1C.vx = ctx.pick(i16, &word);
    obj.unk1C.vy = ctx.pick(i16, &word);
    obj.unk1C.vz = ctx.pick(i16, &word);

    obj.unk2C = ctx.pick(i16, &word);
    obj.unk2E = ctx.pick(i16, &word);
    obj.unk30 = ctx.pick(i16, &word);

    obj.unk2A = ctx.pick(i16, &word);
    obj.unk32 = ctx.pick(i16, &word);
    if (ctx.chance(3)) obj.unk2A = 0;
    if (ctx.chance(3)) obj.unk32 = 0;

    return .{obj};
}

pub fn classify(args: Args) usize {
    const a = args[0].?;
    const lo: usize = @intFromBool(a.unk2A != 0);
    const hi: usize = @intFromBool(a.unk32 != 0);
    return lo + 2 * hi;
}
