//! zig_diff spec: func_800B07DC -> actor_transform.offsetToWorld (44F14.c).
//! Inputs: a random D_800F4538_t (pitch, facing, roll, position: any i16,
//! so roll is rarely 0 and offset + position overflows i16) and a random
//! scratchpad. The GTE calls are recording stubs; MFC2 returns any 32-bit
//! value, so the narrowing of matrix.t to i16 wraps.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").offsetToWorld));

pub const classes = [_][:0]const u8{ "roll-zero", "roll-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    if (ctx.chance(8)) actor.unk0.roll = 0;
    return .{actor};
}

pub fn classify(args: Args) usize {
    return if (args[0].?.unk0.roll == 0) 0 else 1;
}
