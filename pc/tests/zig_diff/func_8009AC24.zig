//! zig_diff spec: func_8009AC24 -> func_8009AC24.func_8009AC24 (30DB0.c).
//! Inputs: a vs_battle_objectData with a random index (-128..127, so the
//! skipped slot is sometimes in range and sometimes not) and a random
//! modelId (negative, zero and the i16 extremes included). The 17-entry
//! D_800F4538 table is set entry by entry: null, or a 0x6E8-byte block whose
//! unk6E6 is often the modelId's bit pattern (a negative modelId matches
//! only under an unsigned compare), and the slot at the object's own index
//! is sometimes a match, so the self-skip is taken. The classes say whether
//! the search finds a match (at 16 or elsewhere), only the object's own slot
//! matches, or nothing does. The function makes no external calls.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009AC24));

pub const classes = [_][:0]const u8{ "not-found", "found", "self-only", "found-last" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.vs_battle_objectData);
    obj.modelId = ctx.pick(i16, &.{ 0, 1, 16, -1, 0x7FFF, -32768 });
    obj.index = ctx.pick(i8, &.{ -1, 0, 1, 15, 16, 17, 127, -128 });
    const table = v.g.D_800F4538();
    const model_bits: u16 = @bitCast(obj.modelId);

    for (0..17) |i| {
        if (ctx.range(u8, 0, 9) < 4) {
            table[i] = null;
            continue;
        }
        const d: *v.D_800F4538_t = @ptrCast(@alignCast(ctx.allocBytes(0x6E8, 16)));
        d.unk6E6 = if (ctx.chance(3)) model_bits else ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFF });
        table[i] = d;
    }

    // Sometimes the object's own slot matches, so the self-skip decides the result.
    if (obj.index >= 0 and obj.index <= 16 and ctx.chance(3)) {
        const d: *v.D_800F4538_t = @ptrCast(@alignCast(ctx.allocBytes(0x6E8, 16)));
        d.unk6E6 = model_bits;
        table[@intCast(obj.index)] = d;
    }

    return .{obj};
}

pub fn classify(args: Args) usize {
    const obj = args[0].?;
    const table = v.g.D_800F4538();
    var self_hit = false;
    for (table, 0..) |entry, i| {
        const d = entry orelse continue;
        if (@as(i32, d.unk6E6) != @as(i32, obj.modelId)) continue;
        const is_self = @as(i32, @intCast(i)) == @as(i32, obj.index);
        if (is_self) {
            self_hit = true;
            continue;
        }
        return if (i == 16) 3 else 1;
    }
    return if (self_hit) 2 else 0;
}
