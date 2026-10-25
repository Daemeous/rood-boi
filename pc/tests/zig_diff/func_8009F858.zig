//! zig_diff spec: func_8009F858 -> func_8009F858.func_8009F858 (30DB0.c).
//! Inputs: an index into the 16-entry D_800F45E0 table. The entry at that index
//! is a null pointer, or a D_800F45E0_t in the arena whose unk6C[8] has an
//! actorId that is either 1 or one of 0, 2..7, and whose unk1 is an edge
//! value (0, 1, 127, 128, 255) or any byte. The other table entries are null.
//! Each class (null, bad-actor, ok) is reached by the C's three returns.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009F858));

pub const classes = [_][:0]const u8{ "null", "bad-actor", "ok" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F45E0();
    for (table) |*e| e.* = null;

    const idx = ctx.range(c_int, 0, 15);
    const obj = ctx.alloc(v.D_800F45E0_t);
    obj.unk6C[8].unk1 = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    const id: u3 = if (ctx.chance(3)) ctx.edge(u3, &.{ 0, 2, 3, 4, 5, 6, 7 }) else 1;
    psx.setBits(&obj.unk6C[8], "actorId", id);
    table[@intCast(idx)] = if (ctx.chance(4)) null else obj;
    return .{idx};
}

pub fn classify(args: Args) usize {
    const e = v.g.D_800F45E0()[@intCast(args[0])] orelse return 0;
    return if (psx.getBits(&e.unk6C[8], "actorId") != 1) 1 else 2;
}
