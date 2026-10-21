//! zig_diff spec: func_800A17BC -> func_800A17BC.func_800A17BC (38C1C.c).
//! Inputs: a slot index (arg0) and an arm index (arg1), with the slot's actor,
//! its object, or neither, and the wepModels entry for the 0x40 arm range.
//! Every global pointer table entry the C can read is set by prepare (null or
//! an arena object). The armature blocks are 8 KB arena blocks so every index
//! the C reads (including -2 and 257) stays inside owned memory. The NULL
//! matrix case (the stub returns null) reads PS1 address 0. The stub's matrix
//! sits at a fixed arena offset, so both runs see the same bytes.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_38C1C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A17BC));

pub const classes = [_][:0]const u8{
    "no-actor-no-obj",
    "obj-armature",
    "actor-0xFF",
    "actor-model-null",
    "actor-model",
    "actor-body",
    "matrix-null",
};

const matrix_off: usize = 96 * 1024;
var null_matrix: bool = false;
var path_class: usize = 0;

pub const stubs = struct {
    pub fn func_800A1DE8(ctx: *zd.Ctx, arg0: c_int, arg1: c_int, arg2: anytype) [*c]v.MATRIX {
        _ = .{ ctx, arg0, arg1, arg2 };
        if (null_matrix) return null;
        return @ptrCast(@alignCast(&zd.zdiff_arena[matrix_off]));
    }
};

fn blob(ctx: *zd.Ctx, comptime T: type) *T {
    return @ptrCast(@alignCast(ctx.allocBytes(8192, 16)));
}

fn objArg1(ctx: *zd.Ctx) c_int {
    const edges = [_]c_int{ -2, -1, 0, 1, 2, 15, 16, 0x3F, 0x40, 0x41, 0x4F, 0x50, 0x7F, 0x80, 0xF0, 0xFE, 0xFF, 0x100, 0x101 };
    if (ctx.chance(2)) return edges[ctx.range(usize, 0, edges.len - 1)];
    return ctx.range(c_int, 0, 255);
}

fn bodyArg1(ctx: *zd.Ctx) c_int {
    if (ctx.chance(6)) return if (ctx.chance(2)) -1 else -2;
    var x = ctx.range(c_int, 0, 255);
    if (x == 0xFF or (x & 0xF0) == 0x40) x = 0x80;
    return x;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const actors = v.g.D_800F4538();
    const objs = v.g.D_800F45E0();
    const models = v.g.vs_battle_wepModels();
    for (actors) |*p| p.* = null;
    for (objs) |*p| p.* = null;
    for (models) |*p| p.* = null;

    null_matrix = ctx.chance(4);

    const kind = ctx.range(u8, 0, 9);
    path_class = switch (kind) {
        0, 1 => 0,
        2, 3 => 1,
        4 => 2,
        5 => 3,
        6, 7 => 4,
        else => 5,
    };

    var arg0: c_int = ctx.range(c_int, 0, 15);
    if (path_class == 3 or path_class == 4) arg0 = ctx.range(c_int, 0, 9);

    const arg1: c_int = switch (path_class) {
        0, 1 => objArg1(ctx),
        2 => 0xFF,
        3, 4 => 0x40 + ctx.range(c_int, 0, 15) + 0x100 * ctx.range(c_int, 0, 1),
        else => bodyArg1(ctx),
    };

    const matrix = ctx.alloc(v.MATRIX);
    const out = ctx.alloc(c_int);

    const idx: usize = @intCast(arg0);
    if (path_class == 1) {
        const obj = ctx.alloc(v.D_800F4538_t);
        psx.put(&obj.unk0.unk68, blob(ctx, v.D_800F4538_unk68));
        objs[idx] = @ptrCast(@alignCast(obj));
    } else if (path_class != 0) {
        const actor = ctx.alloc(v.D_800F4538_t);
        psx.put(&actor.unk0.unk68, blob(ctx, v.D_800F4538_unk68));
        actors[idx] = actor;
        if (path_class == 4) {
            const model = ctx.alloc(v.vs_battle_wepModels_t);
            psx.put(&model.offsets, blob(ctx, v._wepModelOffsets));
            models[idx * 2] = model;
        }
    }

    return .{ arg0, arg1, matrix, out };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (null_matrix) return 6;
    return path_class;
}
