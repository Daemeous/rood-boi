//! zig_diff spec: vs_battle_drawCursor -> draw_cursor.drawCursor (5BF94.c).
//! Inputs: an animation step 0-15 (15 is the one that wraps the returned
//! step to 0 through the mask), and a position from a set of edges (0, -1,
//! small, the 0x7FF/0x800 boundary where the shifted word overflows 16 bits,
//! and the largest and smallest positions whose *16 stays in int range).
//! The step indexes a 16-entry table, so it stays in 0..15. The callee
//! returns a valid arena block of random words (the C stores through it at
//! [4]); the scratchpad's unk8 points into the arena so psx_get is safe.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").drawCursor));

pub const classes = [_][:0]const u8{ "step-15-wraps", "position-negative", "position-nonnegative" };

pub const stubs = struct {
    pub fn vs_battle_setSpriteDefaultTexPage(
        ctx: *zd.Ctx,
        brightnessTransparency: anytype,
        xy: anytype,
        wh: anytype,
        nextPrim: anytype,
    ) [*c]c_uint {
        _ = brightnessTransparency;
        _ = xy;
        _ = wh;
        _ = nextPrim;
        // The callee writes the primitive; the C then overwrites word 4.
        const prim = ctx.alloc([8]c_uint);
        for (prim) |*w| w.* = ctx.value(c_uint);
        return &prim[0];
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch = v.g.vs_scratch();
    psx.put(&scratch.unk8, ctx.alloc(u32));
    const step: c_int = if (ctx.chance(4)) 15 else ctx.range(c_int, 0, 15);
    const position: c_int = if (ctx.chance(2))
        ctx.edge(c_int, &.{ 0, -1, 1, 0x7FF, 0x800, -0x800, 0x07FF_FFFF, -0x0800_0000, 0x1234 })
    else
        ctx.range(c_int, -0x0800_0000, 0x07FF_FFFF);
    return .{ step, position };
}

pub fn classify(args: Args) usize {
    if (args[0] == 15) return 0;
    return if (args[1] < 0) 1 else 2;
}
