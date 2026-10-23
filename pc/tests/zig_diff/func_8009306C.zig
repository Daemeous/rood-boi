//! zig_diff spec: func_8009306C -> func_8009306C.func_8009306C (2842C.c).
//! Inputs: one random func_80092F74_t in the arena. Its velocities and
//! positions are picked from edges (0, +-1, the i16 extremes) and random
//! values, so the halvings, the `&= 0xFFF` masks and the i16 wraps are all
//! exercised. unk2A takes 0, negative, 1, 2 and 32767 values. pad takes -1,
//! 0, 1, 2 (so the increment makes 3 and `pad < 3` sees equal sides) and
//! 32767 (which wraps). unk32 takes 0, +-1 and 32767.
//! The floor lookup stub returns any 16-bit value, and half the time it
//! writes the object's unk10 to the 15-bit height it implies times 16. That
//! makes `height < unk10 / 16` see equal sides, which the C reads back after
//! the call. The func_80092EDC stub writes unkA, so a dropped call changes the
//! state. The `(unk2A >= 0)` arm of the ABS macro is unreachable: the C only
//! evaluates it after `unk2A > 0`. That arm needs a coverage waiver (see the toml).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009306C));

/// The object prepare built, for the stubs to write back into.
var cur: ?*v.func_80092F74_t = null;

pub const classes = [_][:0]const u8{
    "unk2A-zero",
    "unk2A-negative",
    "bounce-pad-two",
    "bounce-pad-below-two",
    "bounce-pad-above-two",
};

pub const stubs = struct {
    /// The floor height lookup: any 16-bit result. The C takes the low 15 bits
    /// sign-extended as the height and, in some calls, compares it with
    /// unk10 / 16 after this returns, so the stub sets unk10 to match.
    pub fn func_8008DC7C(ctx: *zd.Ctx, a: anytype, b: anytype) c_short {
        _ = a;
        _ = b;
        const r = ctx.value(c_short);
        const h: i32 = (@as(i32, r) << 17) >> 17;
        if (cur) |o| {
            if (ctx.value(u1) == 1) o.unk10 = h * 16;
        }
        return r;
    }

    /// The post-update routine: it changes unkA, which the harness compares.
    pub fn func_80092EDC(ctx: *zd.Ctx, p: anytype) void {
        const o: *v.func_80092F74_t = @ptrCast(@alignCast(p));
        o.unkA = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_80092F74_t);
    cur = obj;
    obj.unkC = ctx.pick(i32, &.{ 0, 15, -15, 0x7fff_ffff, -0x8000_0000 });
    obj.unk14 = ctx.pick(i32, &.{ 0, 15, -15, 0x7fff_ffff, -0x8000_0000 });
    obj.unk10 = ctx.pick(i32, &.{ 0, 15, -15, 0x7fff_ffff, -0x8000_0000 });
    obj.unk24 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    obj.unk26 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    obj.unk28 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    obj.unk2C = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    obj.unk2E = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    obj.unk30 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    obj.unk2A = ctx.pick(i16, &.{ 0, 1, 2, -1, 32767, -32768 });
    obj.unk32 = ctx.pick(i16, &.{ 0, 1, -1, 32767 });
    obj.unk1C.pad = ctx.pick(i16, &.{ 0, 1, 2, -1, 32767 });
    return .{obj};
}

pub fn classify(args: Args) usize {
    const obj = args[0].?;
    if (obj.unk2A == 0) return 0;
    if (obj.unk2A < 0) return 1;
    if (obj.unk1C.pad == 2) return 2;
    if (obj.unk1C.pad < 2) return 3;
    return 4;
}
