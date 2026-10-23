//! zig_diff spec: func_800AC500 -> func_800AC500.func_800AC500 (40564.c).
//! Inputs: an arena D_800F4538_t with random bytes (so unk18D0 starts
//! nonzero) whose unk68 points at an arena block of random bytes, big enough
//! for every armature the loop can reach. nBones is drawn from 0, 1, 2, 127,
//! 128, 255 and any other byte: an empty loop, a single pass, the signed-byte
//! boundary (128 and above read negative as a signed char) and the maximum
//! count. The loop bound and the armature stores are checked through the
//! arena bytes, and unk18D0[0..1] through the same compare.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AC500));

pub const classes = [_][:0]const u8{ "no-bones", "one-bone", "high-bones", "many-bones" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.D_800F4538_t);
    arg.unk0.nBones = ctx.pick(u8, &.{ 0, 1, 2, 127, 128, 255 });
    // Room for armatures[0..255] (64-byte header, 16-byte entries).
    const table: *v.D_800F4538_unk68 = @ptrCast(@alignCast(ctx.allocBytes(8192, 16)));
    psx.put(&arg.unk0.unk68, table);
    return .{arg};
}

pub fn classify(args: Args) usize {
    const n = args[0].?.unk0.nBones;
    if (n == 0) return 0;
    if (n == 1) return 1;
    if (n >= 128) return 2;
    return 3;
}
