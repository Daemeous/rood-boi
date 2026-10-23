//! zig_diff spec: func_800AAD4C -> func_800AAD4C.func_800AAD4C (40564.c).
//! Inputs: an actor id 0-16 (the table's length; the C reads D_800F4538[actorId]
//! unchecked, so nothing outside it is built) whose entry is an arena actor or
//! null (the C's NULL test, about one run in ten). The index is from {0, 1, 2, 3, -1}:
//! 2 and 3 take the `index >= 2` return, and -1 passes the signed test and writes
//! unk180C[-1], which lies inside the actor struct. The mode is mostly 1, and
//! value is from edges (0, 1, 15, 16, 17, -1, the int extremes) or random. Half of
//! the present actors get a preset slot (for index 0 or 1): unk0_4 from {0,1,2,3,15},
//! and unk0_0 equal to the value when it is 0-15 (the mode 1 early return) or random.
//! Together these reach the same-slot return, the rewrite, the mode 2 inner switch
//! (0, 1, 2 and the default) and the mode default. No callee is called, so no stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AAD4C));

pub const classes = [_][:0]const u8{
    "no-actor",
    "bad-index",
    "mode1-zero",
    "mode0",
    "mode1-new",
    "mode1-same",
    "mode2",
    "mode-other",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const actorId = ctx.range(c_int, 0, 16);
    const index = ctx.edge(c_int, &.{ 0, 1, 2, 3, -1 });
    const mode = ctx.pick(c_int, &.{ 0, 1, 1, 1, 2, 3, -1 });
    var value = ctx.pick(c_int, &.{ 0, 0, 1, 2, 7, 15, 16, 17, -1, 0x7fffffff, -0x80000000 });
    if (ctx.chance(8)) value = 0;

    const actor = ctx.alloc(v.D_800F4538_t);
    const present = !ctx.chance(10);
    table[@intCast(actorId)] = if (present) actor else null;

    if (present and index < 2 and ctx.chance(2)) {
        const slot = psx.at(&actor.unk180C, index);
        psx.setBits(slot, "unk0_4", ctx.edge(u4, &.{ 0, 1, 1, 2, 3, 15 }));
        const same = value >= 0 and value <= 15 and ctx.chance(2);
        psx.setBits(slot, "unk0_0", if (same) @as(u4, @intCast(value)) else ctx.int(u4));
    }

    return .{ actorId, index, value, mode };
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F4538();
    const actor = table[@intCast(args[0])] orelse return 0;
    if (args[1] >= 2) return 1;
    if (args[3] == 1 and args[2] == 0) return 2;
    if (args[3] == 0) return 3;
    if (args[3] == 2) return 6;
    if (args[3] != 1) return 7;
    const slot = psx.at(&actor.unk180C, args[1]);
    if (@as(c_int, psx.getBits(slot, "unk0_4")) == 1 and @as(c_int, psx.getBits(slot, "unk0_0")) == args[2]) return 5;
    return 4;
}
