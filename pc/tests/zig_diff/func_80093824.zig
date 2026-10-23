//! zig_diff spec: func_80093824 -> func_80093824.func_80093824 (2842C.c).
//! Inputs: arg0 is a valid index into D_800F1DD8 (96 entries), whose unk4 points
//! at an arena D_800F1DD8_t2 with random fields. unk7 is 2 half the time and
//! another value otherwise, so both branches run. unk0 is an edge offset (negative,
//! zero, positive) into a 512-byte arena geometry buffer that geometrySection
//! points into, so both stores land inside it. unk20 and unkC take the
//! negation edges (0, +-1, INT_MAX, INT_MIN) and the random values cover the rest.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80093824));

pub const classes = [_][:0]const u8{ "unk7-is-2", "unk7-other-unk20-min", "unk7-other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // Geometry buffer: 128 ints in the arena; geometrySection points at element 32,
    // so unk0 in [-32, 32] and the +4/+6 stores stay inside it.
    const buf: [*]i32 = @ptrCast(@alignCast(ctx.allocBytes(512, 16)));
    psx.put(&v.g.vs_battle_roomData().geometrySection, &buf[32]);

    const t2 = ctx.alloc(v.D_800F1DD8_t2);
    t2.unk0 = ctx.edge(i32, &.{ -32, -1, 0, 1, 16, 32 });
    t2.unk7 = if (ctx.chance(2)) 2 else ctx.edge(u8, &.{ 0, 1, 3, 255 });
    t2.unk20 = ctx.pick(i32, &.{ 0, 1, -1, std.math.maxInt(i32), std.math.minInt(i32) });
    t2.unkC = ctx.pick(i32, &.{ 0, 1, -1, std.math.maxInt(i32), std.math.minInt(i32) });

    const idx: c_int = ctx.range(c_int, 0, 95);
    psx.put(&v.g.D_800F1DD8()[@as(usize, @intCast(idx))].unk4, t2);
    return .{idx};
}

pub fn classify(args: Args) usize {
    const idx: usize = @intCast(args[0]);
    const t2 = psx.get(v.g.D_800F1DD8()[idx].unk4).?;
    if (t2.unk7 == 2) return 0;
    if (t2.unk20 == std.math.minInt(i32)) return 1;
    return 2;
}
