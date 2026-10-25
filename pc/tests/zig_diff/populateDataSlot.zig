//! zig_diff spec: vs_battle_populateDataSlot -> populate_data_slot.populateDataSlot (30DB0.c).
//! Inputs: a valid object (the C dereferences it, so it is never null) and the
//! 16-slot object data table with every dataType set by prepare: nonzero with
//! and without the sign bit, a zero at a chosen free slot (first, middle, last),
//! zeros after it sometimes (the first free slot must win), or no free slot at
//! all (the loop runs to i == 16). Some objects alias a table slot, so the copy
//! can overlap itself. No callees, so no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").populateDataSlot));

pub const classes = [_][:0]const u8{
    "free-slot-0",
    "free-slot-middle",
    "free-slot-15",
    "no-free-slot",
    "alias-into-table",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const slots = v.g.vs_battle_objectDataSlots();

    // The first free slot: 0 often, 15 sometimes, none sometimes, else middle.
    const free: ?usize = switch (ctx.range(u8, 0, 7)) {
        0, 1 => 0,
        2 => 15,
        3 => null,
        else => ctx.range(usize, 1, 14),
    };

    for (slots, 0..) |*s, j| {
        s.dataType = ctx.edge(u8, &.{ 0x80, 0x01, 0xFF, 0x7F });
        if (free) |f| {
            if (j == f or (j > f and ctx.chance(2))) s.dataType = 0;
        }
    }

    const obj: *v.vs_battle_objectData = if (ctx.chance(6))
        &slots[ctx.range(usize, 0, 15)]
    else
        ctx.alloc(v.vs_battle_objectData);

    return .{obj};
}

pub fn classify(args: Args) usize {
    const slots = v.g.vs_battle_objectDataSlots();
    const obj = @intFromPtr(args[0].?);
    const base = @intFromPtr(&slots[0]);
    if (obj >= base and obj < base + @sizeOf([16]v.vs_battle_objectData)) return 4;
    for (slots, 0..) |s, i| {
        if (s.dataType == 0) {
            if (i == 0) return 0;
            if (i == 15) return 2;
            return 1;
        }
    }
    return 3;
}
