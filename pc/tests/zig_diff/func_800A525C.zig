//! zig_diff spec: func_800A525C -> func_800A525C.func_800A525C (3A1A0.c).
//! Inputs: one D_800F4538_t in the arena, its bytes random, with unk0.unkF
//! zero half the time and otherwise an edge value (1, 0x7F, 0x80, 0xFF) or a
//! random byte. Both sides of the `unkF == 0` test are taken, and unkF's sign
//! bit is both set and clear when nonzero. The store fields (unk1848 unk0,
//! unk4, unk6, unk8) are random before the call, so a dropped or misplaced
//! store shows in the state comparison.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A525C));

pub const classes = [_][:0]const u8{ "unkF-zero", "unkF-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    obj.unk0.unkF = if (ctx.chance(2)) 0 else ctx.pick(u8, &.{ 1, 0x7F, 0x80, 0xFF });
    return .{obj};
}

pub fn classify(args: Args) usize {
    return if (args[0].?.unk0.unkF == 0) 0 else 1;
}
