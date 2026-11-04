//! zig_diff spec: func_800B3A68 -> pc.project_object_vertices.projectObjectVertices (D2: pc/src/actor_vertices.c).
//! Inputs: an object block whose byte 1 (the group count) is zero, small or
//! any byte; a model block whose words at 0x34 and 0x38 (the group and vertex
//! tables, guest pointers) are arena addresses or, a quarter of the time,
//! zero; a matrix block; any near-clip argument. The stubs of
//! pc_project_bone_vertices (returns any word) and of the native-matrices
//! hook record every argument, so the two table pointers, the count, the
//! matrices and the screen and depth arrays (D_800F2458 and 768 words on)
//! are compared.
const std = @import("std");
const zd = @import("zdiff");
const c = @import("c_actor_vertices");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").projectObjectVertices));

pub const classes = [_][:0]const u8{ "no-groups", "groups" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const object = ctx.allocBytes(0x40, 16);
    for (object[0..0x40]) |*byte| byte.* = ctx.int(u8);
    object[1] = ctx.pick(u8, &.{ 0, 1, 2, 0xFF });
    const model = ctx.allocBytes(0x40, 16);
    for (model[0..0x40]) |*byte| byte.* = ctx.int(u8);
    const groups: u32 = if (ctx.chance(4)) 0 else @intCast(@intFromPtr(ctx.allocBytes(64, 16)));
    const vertices: u32 = if (ctx.chance(4)) 0 else @intCast(@intFromPtr(ctx.allocBytes(64, 16)));
    std.mem.writeInt(u32, model[0x34..][0..4], groups, .little);
    std.mem.writeInt(u32, model[0x38..][0..4], vertices, .little);
    const matrices: [*c]c.MATRIX = @ptrCast(@alignCast(ctx.allocBytes(0x100, 16)));
    return .{ object, matrices, ctx.int(c_int), model };
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[0][1] != 0);
}
