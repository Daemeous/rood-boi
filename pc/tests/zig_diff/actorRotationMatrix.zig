//! zig_diff spec: func_800B17F0 -> pc.actor_rotation_matrix.actorRotationMatrix (D2: pc/src/boot_sdk.c).
//! Inputs: an SVECTOR of angles (edges or any short) and a result MATRIX in the
//! arena. rsin, rcos, MTC2, GPF and MFC2 are recording stubs returning drawn
//! ints (so the products wrap and the shifts take every sign): the three GPF
//! stages' inputs and the matrix built from the stub values are what show.
const std = @import("std");
const zd = @import("zdiff");
const c = @import("c_boot_sdk");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").actorRotationMatrix));

pub const classes = [_][:0]const u8{ "edge-angles", "any-angles" };

var edge_angles = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    const angles = ctx.alloc(c.SVECTOR);
    edge_angles = ctx.chance(2);
    const edges = [_]c_short{ 0, 0x400, 0x800, 0xc00, 0x7fff, -0x8000, -1, 0x1000 };
    angles.vx = if (edge_angles) ctx.edge(c_short, &edges) else ctx.int(c_short);
    angles.vy = if (edge_angles) ctx.edge(c_short, &edges) else ctx.int(c_short);
    angles.vz = if (edge_angles) ctx.edge(c_short, &edges) else ctx.int(c_short);
    angles.pad = ctx.int(c_short);
    return .{ angles, ctx.alloc(c.MATRIX) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return @intFromBool(!edge_angles);
}
