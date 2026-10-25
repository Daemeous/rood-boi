//! zig_diff spec: func_800A0104 -> func_800A0104.func_800A0104 (30DB0.c).
//! Inputs: an index 0-15 into D_800F45E0 (the entry is null about one time in
//! six, otherwise a fresh arena object whose unk6C[8].actorId is an edge from
//! {0,1,2,2,3,7}, so both sides of `actorId >= 2` are equal often), and a
//! material 0-23 (the range the 96-byte D_800E9030 table covers, with the
//! ends as edges). The scratchpad and the object's bytes keep their random
//! fill, so the stored unk2, unk18, unk24/unk26 and unk28 are checked against
//! what was there before. RotMatrixYXZ_gte is a stub that fills its matrix
//! with values from ctx.value and returns null.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A0104));

pub const classes = [_][:0]const u8{ "null-object", "actor-busy", "actor-free" };

pub const stubs = struct {
    pub fn RotMatrixYXZ_gte(ctx: *zd.Ctx, r: anytype, m: anytype) [*c]v.MATRIX {
        _ = r;
        const mat: *v.MATRIX = @ptrCast(@alignCast(m));
        for (&mat.m) |*row| {
            for (row) |*e| e.* = ctx.value(i16);
        }
        mat.t = .{ ctx.value(i32), ctx.value(i32), ctx.value(i32) };
        return null;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    // Indices past the 16-entry table are not in memory the harness owns.
    const index = ctx.range(c_int, 0, 15);
    // D_800E9030 has 96 bytes: material * 4 + 2 must stay below 96.
    const material = if (ctx.chance(4)) ctx.edge(c_int, &.{ 0, 23 }) else ctx.range(c_int, 0, 23);
    const table = v.g.D_800F45E0();
    if (ctx.chance(6)) {
        table[@intCast(index)] = null;
    } else {
        const obj = ctx.alloc(v.D_800F45E0_t);
        psx.setBits(&obj.unk6C[8], "actorId", ctx.pick(u3, &.{ 0, 1, 2, 2, 3, 7 }));
        table[@intCast(index)] = obj;
    }
    return .{ index, material };
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F45E0();
    const obj = table[@as(usize, @intCast(args[0]))] orelse return 0;
    return if (psx.getBits(&obj.unk6C[8], "actorId") >= 2) 1 else 2;
}
