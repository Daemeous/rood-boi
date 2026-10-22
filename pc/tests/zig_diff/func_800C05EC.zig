//! zig_diff spec: func_800C05EC -> func_800C05EC.func_800C05EC (573B8.c).
//! Inputs: the battle state pointer D_800EB9B8 (a fresh D_800EB9B8_t in the
//! arena, never null, as the C dereferences it unconditionally), the armed
//! flag D_800EB9AC (0, 1, 0x7F, 0x80, 0xFF, so it is nonzero with and without
//! the sign bit), arg0 with unk0 equal to 1 or not (0, 2, 0x80, 0xFF), arg1
//! (non-null whenever the flag is armed, since the armed path reads arg1->unk0
//! unconditionally; in the idle path null, a fresh block, or aliasing the
//! target's unk0, which the C stores over first), arg2 over 0/1/0x80/0xFF, and
//! arg3 over edges including values wider than a byte.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C05EC));

pub const classes = [_][:0]const u8{
    "armed",
    "idle-no-arg1",
    "idle-arg1",
    "idle-arg1-aliases-target",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const target = ctx.alloc(v.D_800EB9B8_t);
    v.g.D_800EB9B8().* = target;
    const armed = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF, 0x7F });
    v.g.D_800EB9AC().* = armed;

    const arg0 = ctx.alloc(v.D_800EB9B8_t2);
    arg0.unk0 = ctx.pick(u8, &.{ 1, 1, 0, 2, 0x80, 0xFF });

    var arg1: ?*v.D_800EB9B8_t2 = null;
    if (armed != 0) {
        // The armed path dereferences arg1: it must be valid here.
        if (ctx.chance(2)) {
            arg1 = &target.unk0;
        } else {
            const b = ctx.alloc(v.D_800EB9B8_t2);
            b.unk0 = ctx.pick(u8, &.{ 1, 0, 2, 0x80, 0xFF });
            arg1 = b;
        }
    } else if (!ctx.chance(3)) {
        if (ctx.chance(2)) {
            arg1 = &target.unk0;
        } else {
            const b = ctx.alloc(v.D_800EB9B8_t2);
            b.unk0 = ctx.pick(u8, &.{ 1, 0, 2, 0x80, 0xFF });
            arg1 = b;
        }
    }

    const arg2 = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
    const arg3 = ctx.pick(c_int, &.{ 0, -1, 1, 255, 256, 0x7FFF_FFFF, -0x8000_0000 });
    return .{ arg0, arg1, arg2, arg3 };
}

pub fn classify(args: Args) usize {
    if (v.g.D_800EB9AC().* != 0) return 0;
    const a1 = args[1] orelse return 1;
    const target = v.g.D_800EB9B8().*.?;
    if (@intFromPtr(a1) == @intFromPtr(&target.unk0)) return 3;
    return 2;
}
