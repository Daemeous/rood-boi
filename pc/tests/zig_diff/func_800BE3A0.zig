//! zig_diff spec: func_800BE3A0 -> func_800BE3A0.func_800BE3A0 (4A0A8.c).
//! Inputs: the camera-state global points at a random D_800F4BA4_t in the
//! arena, every byte random, so the eye and lookAt modes and the follow and
//! following flags start with arbitrary values. The C has no branches and
//! makes no calls; the spec checks the mode stores (0xF) and the four
//! follow/following clears, each of which the mutations drop or misdirect.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BE3A0));

pub const classes = [_][:0]const u8{"camera-block"};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.D_800F4BA4_t);
    v.g.D_800F4BA4().* = state;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
