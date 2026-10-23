//! zig_diff spec: func_80093788 -> func_80093788.func_80093788 (2842C.c).
//! Inputs: an index arg0 from the edges 0..100 (the C reads D_800F1DD8 only
//! below 0x40, so 64 and 95-100 cover the bound and the skipped range; the
//! table has 96 entries, and entries 64 and up are set but never read). Each
//! entry gets unk1 == 6 most of the time, otherwise 0, 5, 7 or 255, and unk0
//! zero or nonzero with and without the sign bit. The entry's unk4 points at
//! an arena D_800F1DD8_t2 whose unk0 is a 4-aligned offset into a 1100-byte
//! geometry block in the arena (the C writes geometry[offset/4 + 0] and [5]),
//! with unk20 at minInt, 0, -1, 1 or random, so the negation wraps. The
//! geometry section pointer is set through vs_battle_roomData. No calls are
//! made, so there are no stubs. Negative indices are not generated: the C
//! would read the table before its start.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80093788));

pub const classes = [_][:0]const u8{ "out-of-range", "not-six", "six-unit0-zero", "six-unit0-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: c_int = ctx.edge(c_int, &.{ 0, 1, 31, 63, 64, 65, 95, 96, 100 });
    const tbl = v.g.D_800F1DD8();
    const room = v.g.vs_battle_roomData();

    const geom: *i32 = @ptrCast(@alignCast(ctx.allocBytes(1100, 4)));
    psx.put(&room.geometrySection, geom);

    if (arg0 < 96) {
        const e = &tbl[@intCast(arg0)];
        e.unk1 = if (ctx.chance(5)) ctx.edge(u8, &.{ 0, 5, 7, 255 }) else 6;
        e.unk0 = if (ctx.chance(3)) 0 else ctx.pick(u8, &.{ 1, 0x80, 0xFF });

        const t2 = ctx.alloc(v.D_800F1DD8_t2);
        t2.unk0 = @as(i32, ctx.range(u8, 0, 255)) * 4;
        t2.unk20 = ctx.pick(i32, &.{ std.math.minInt(i32), 0, -1, 1 });
        t2.unk18 = ctx.pick(i32, &.{ 0, -1, std.math.minInt(i32), 12345 });
        t2.unk1C = ctx.pick(i32, &.{ 0, 7, std.math.minInt(i32) });
        t2.unkC = ctx.pick(i32, &.{ std.math.minInt(i32), 0, 1 });
        psx.put(&e.unk4, t2);
    }
    return .{arg0};
}

pub fn classify(args: Args) usize {
    const arg0 = args[0];
    if (arg0 >= 0x40) return 0;
    const e = v.g.D_800F1DD8()[@intCast(arg0)];
    if (e.unk1 != 6) return 1;
    return if (e.unk0 == 0) 2 else 3;
}
