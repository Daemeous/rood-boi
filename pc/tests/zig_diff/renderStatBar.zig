//! zig_diff spec: _renderStatBar -> render_stat_bar.renderStatBar (5BF94.c).
//! Inputs: colorIndex 0-3 (the four-entry tables it indexes): 0 and 1 take the
//! value-and-digit branch, 2 the digit-less one, 3 also rounds current. max is
//! zero, positive or negative, with the edges that make the `<< 6` and the
//! division wrap. current spans the int range with the edges that wrap the
//! `<< 6` and the `+ 0xFF`. The scratchpad-side prim buffer lives in the arena:
//! unk4 points into it so the -0xC step and the [4] store land in valid memory.
//! The setSprite stub returns either its prim argument or a fresh arena block,
//! so a store through the wrong pointer shows. colorIndex's sign bit (the
//! `& 2` test) is unreachable with four-entry tables: waived in specs.toml.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").renderStatBar));

pub const classes = [_][:0]const u8{ "max-zero", "bar-plain", "bar-digit", "bar-color3", "negative-max" };

pub const stubs = struct {
    pub fn vs_battle_setSpriteDefaultTexPage(
        ctx: *zd.Ctx,
        brightnessTransparency: anytype,
        xy: anytype,
        wh: anytype,
        nextPrim: anytype,
    ) @TypeOf(nextPrim) {
        _ = brightnessTransparency;
        _ = xy;
        _ = wh;
        // The C stores through the returned pointer: return the prim argument
        // half the time, a different arena block the other half.
        if (ctx.value(u1) == 1) return nextPrim;
        return @ptrCast(@alignCast(ctx.allocBytes(32, 16)));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch = v.g.vs_scratch();
    // prim = unk4 - 12 u_longs (48 bytes); the store lands at prim + 16 bytes.
    const block: [*]u8 = ctx.allocBytes(256, 16);
    psx.put(&scratch.unk4, @as(*anyopaque, @ptrCast(block + 128)));

    const colorIndex = ctx.range(c_int, 0, 3);
    const current = ctx.pick(c_int, &.{
        0,                       1,                   -1,                  255,
        256,                     -255,                1 << 25,             -(1 << 25),
        0x7FFF_FFF,              std.math.minInt(c_int), std.math.maxInt(c_int),
    });
    const max = ctx.pick(c_int, &.{
        0,                       0,                   1,                   -1,
        2,                       3,                   100,                 255,
        256,                     -100,                std.math.minInt(c_int), std.math.maxInt(c_int),
    });
    return .{ colorIndex, current, max };
}

pub fn classify(args: Args) usize {
    const colorIndex = args[0];
    const max = args[2];
    if (max == 0) return 0;
    if (colorIndex == 3) return 3;
    if (colorIndex == 2) return 2;
    if (max < 0) return 4;
    return 1;
}
