//! zig_diff spec: func_8009195C -> func_8009195C.func_8009195C (2842C.c).
//! Inputs: vs_battle_roomData.geometrySection either null or an arena i32
//! header holding a count from -2147483648 to 2147483647 (small counts
//! mostly, so the index arithmetic never overflows). The argument is an edge
//! around 0, -1, the count and its neighbours (the `arg0 < v1` equality
//! side), the INT_MIN extreme, or a random index in [-4096, 4096].
//! Classes: no geometry section, negative index, index at or past the count,
//! index inside the section.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009195C));

pub const classes = [_][:0]const u8{ "no-geometry", "negative", "out-of-range", "in-range" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const room = v.g.vs_battle_roomData();

    // The section count: mostly an edge, sometimes a small random value.
    const count: i32 = if (ctx.chance(3))
        ctx.range(i32, -8, 8)
    else
        ctx.edge(i32, &.{ -2147483648, -1, 0, 1, 2, 7, 1000, 0x7FFFFFFF });

    // Index: edges around 0, -1, the count and the extremes, clamped so that
    // top - 1 and top + 1 never overflow and arg * 16 stays in range.
    const top: i32 = std.math.clamp(count, -4096, 4096);
    var arg: i32 = ctx.edge(i32, &.{ 0, 1, -1, -2147483648, top - 1, top, top + 1, 4096 });
    if (ctx.chance(4)) arg = ctx.range(i32, -4096, 4096);

    const hdr = ctx.alloc(i32);
    hdr.* = count;
    psx.put(&room.geometrySection, if (ctx.chance(4)) null else hdr);

    return .{arg};
}

pub fn classify(args: Args) usize {
    const geo = psx.get(v.g.vs_battle_roomData().geometrySection) orelse return 0;
    const x = args[0];
    if (x < 0) return 1;
    if (x >= geo.*) return 2;
    return 3;
}
