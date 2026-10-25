//! zig_diff spec: func_8009F898 -> func_8009F898.func_8009F898 (30DB0.c).
//! Inputs: every entry of vs_battle_wepModels is random null or a fresh arena
//! model; arg0 is 0..9 so the table index arg0*2 stays inside the 20 entries
//! (the C reads the entry unchecked, so an out-of-table index is not a valid
//! input); arg1 and arg2 are any c_int, so the 1-bit unk8_6 store sees both
//! parities and arg2's low byte (the u8 store) sees both sides of bit 7.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009F898));

pub const classes = [_][:0]const u8{ "no-model", "model-bit7-set", "model-bit7-clear" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // Every entry is set, so the mutated C's odd-slot read is a valid pointer too.
    const table = v.g.vs_battle_wepModels();
    for (table) |*slot| {
        slot.* = if (ctx.chance(5)) null else ctx.alloc(v.vs_battle_wepModels_t);
    }
    const arg0 = ctx.range(c_int, 0, 9);
    const arg1 = ctx.pick(c_int, &.{ 0, 1, 2, -1, 0x40, 0x7FFF_FFFF, -2147483648 });
    const arg2 = ctx.pick(c_int, &.{ 0, 0x7F, 0x80, 0xFF, 0x100, -1, 0x7FFF_FFFF });
    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const idx: usize = @intCast(args[0] * 2);
    if (v.g.vs_battle_wepModels()[idx] == null) return 0;
    return if ((args[2] & 0x80) != 0) 1 else 2;
}
