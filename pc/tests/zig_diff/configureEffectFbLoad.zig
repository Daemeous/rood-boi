//! zig_diff spec: vs_battle_configureEffectFbLoad -> configure_effect_fb_load.configureEffectFbLoad (6F014.c).
//! The inputs are four random c_ints and an index drawn from 0..11. Indices 0..7 land
//! in the arrays; 8..11 write past loadContexts, slots and isClut, into the
//! neighbouring fields of D_800F5638, as the C does. The body has no calls and no
//! branches, so the spec only has to make every store observable.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").configureEffectFbLoad));

pub const classes = [_][:0]const u8{ "index-in-range", "index-past-end" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // Random values for the four stored operands; the index is kept in 0..11 so
    // every write stays inside the D_800F5638 block the harness owns.
    const index = ctx.range(c_int, 0, 11);
    return .{
        ctx.int(c_int),
        ctx.int(c_int),
        ctx.int(c_int),
        ctx.int(c_int),
        index,
    };
}

pub fn classify(args: Args) usize {
    const index = args[4];
    return if (index >= 0 and index < 8) 0 else 1;
}
