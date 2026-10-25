//! zig_diff spec: func_80099854 -> func_80099854.func_80099854 (30DB0.c).
//! Inputs: arg0 zero (the early return), and otherwise the edge values 1, -1,
//! 255, 256 (material truncates to 0), 257, INT_MAX and INT_MIN, or random
//! bits. The objectData slot table is built so the first free slot (dataType
//! 0) is any of 0..15, or there is none. Slots before it are nonzero; slots
//! after it are random bytes, so later zeros must not be picked. The C calls
//! nothing, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80099854));

pub const classes = [_][:0]const u8{ "arg-zero", "free-first-slot", "free-later-slot", "no-free-slot" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const slots = v.g.vs_battle_objectDataSlots();
    // 16 means no free slot: the loop runs to i == 16 and slot stays -1.
    const first_free = ctx.range(u8, 0, 16);
    for (slots, 0..) |*s, i| {
        if (i < first_free) {
            s.dataType = ctx.range(u8, 1, 255);
        } else if (i == first_free) {
            s.dataType = 0;
        } else {
            s.dataType = ctx.int(u8);
        }
    }
    const arg: c_int = if (ctx.chance(5))
        0
    else
        ctx.pick(c_int, &.{ 1, -1, 255, 256, 257, 0x7FFF_FFFF, -2147483648 });
    return .{arg};
}

pub fn classify(args: Args) usize {
    if (args[0] == 0) return 0;
    const slots = v.g.vs_battle_objectDataSlots();
    for (slots, 0..) |s, i| {
        if (s.dataType == 0) return if (i == 0) 1 else 2;
    }
    return 3;
}
