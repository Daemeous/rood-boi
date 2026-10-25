//! zig_diff spec: vs_battle_getEmptyObjectDataSlot -> get_empty_object_data_slot.getEmptyObjectDataSlot (30DB0.c).
//! Inputs: the 16 slots of vs_battle_objectDataSlots, each with a random
//! dataType. The first zero dataType is placed at a random slot 0-15, or
//! no slot is zero (a quarter of the runs), so the loop runs to its
//! `i < 16` exit, the function returns -1, and the loop's equal-sides
//! case occurs often. Slots after the first zero keep random bytes, which
//! may also be zero. The classes name where the first empty slot sits.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").getEmptyObjectDataSlot));

pub const classes = [_][:0]const u8{ "first-slot", "middle-slot", "last-slot", "none" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const slots = v.g.vs_battle_objectDataSlots();
    const empty: usize = if (ctx.chance(4)) 16 else ctx.range(usize, 0, 15);
    for (slots, 0..) |*s, i| {
        if (i < empty) {
            s.dataType = ctx.range(u8, 1, 255);
        } else if (i == empty) {
            s.dataType = 0;
        } else {
            s.dataType = ctx.int(u8);
        }
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const slots = v.g.vs_battle_objectDataSlots();
    for (slots, 0..) |s, i| {
        if (s.dataType == 0) {
            if (i == 0) return 0;
            if (i == 15) return 2;
            return 1;
        }
    }
    return 3;
}
