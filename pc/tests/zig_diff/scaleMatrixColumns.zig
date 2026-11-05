//! zig_diff spec: func_800B396C -> pc.scale_matrix_columns.scaleMatrixColumns (D2: pc/src/boot_sdk.c).
//! Inputs: a MATRIX of random bytes and a scale VECTOR of edge or random ints.
//! MTC2, GPF and MFC2 are recording stubs: each column's factor and entries
//! go in, the MFC2 values come back as the column.
const std = @import("std");
const zd = @import("zdiff");
const c = @import("c_boot_sdk");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").scaleMatrixColumns));

pub const classes = [_][:0]const u8{"scale"};

pub fn prepare(ctx: *zd.Ctx) Args {
    const scale = ctx.alloc(c.VECTOR);
    scale.vx = ctx.pick(c_int, &.{ 0, 0x1000, -0x1000, 1 });
    scale.vy = ctx.pick(c_int, &.{ 0, 0x1000, -0x1000, 1 });
    scale.vz = ctx.pick(c_int, &.{ 0, 0x1000, -0x1000, 1 });
    return .{ ctx.alloc(c.MATRIX), scale };
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
