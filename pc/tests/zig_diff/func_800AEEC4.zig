//! zig_diff spec: func_800AEEC4 -> func_800AEEC4.func_800AEEC4 (44F14.c).
//! Inputs: a D_800F4538_t in the arena. The step block (unk17E4: mode unk0,
//! position unk1/unk2, remaining duration unk3) and the target block (unk17E8)
//! are random bytes, with the mode 1 most of the time, the duration equal to
//! tickspeed/2 in about a third of the cases where that fits a byte (the C's
//! `<` then sees equal sides), below and above it, and 0 sometimes. The two
//! deltas are zero about a quarter of the time, and both signs otherwise.
//! tickspeed is an edge value or -1024..1024: bounded so that a byte delta
//! times tickspeed/2 never overflows the C's int (UB in C). No callees.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AEEC4));

pub const classes = [_][:0]const u8{
    "early-return",
    "duration-equals-half",
    "duration-below-half",
    "duration-above-half",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const tick = v.g.vs_gametime_tickspeed();

    const edges = [_]i32{ 0, 1, 2, 3, -1, -2, -3, 254, 255, 256, 510, 511, 512, -510, -511, -512, 1024, -1024 };
    tick.* = if (ctx.chance(2)) edges[ctx.range(usize, 0, edges.len - 1)] else ctx.range(i32, -1024, 1024);

    // Mode 1 most of the time: anything else takes the early return.
    actor.unk17E4.unk0 = if (ctx.chance(5)) ctx.int(u8) else 1;

    // Duration: equal to tickspeed/2 sometimes (when it fits a byte), else an edge or random.
    const half: i32 = @divTrunc(tick.*, 2);
    if (ctx.chance(3) and half >= 1 and half <= 255) {
        actor.unk17E4.unk3 = @intCast(half);
    } else {
        actor.unk17E4.unk3 = ctx.pick(u8, &.{ 0, 1, 2, 127, 128, 255 });
    }

    // Deltas: the target equals the position sometimes (zero delta), else random.
    actor.unk17E4.unk2 = ctx.int(u8);
    actor.unk17E8.unk2 = if (ctx.chance(4)) actor.unk17E4.unk2 else ctx.int(u8);
    actor.unk17E4.unk1 = ctx.int(u8);
    actor.unk17E8.unk1 = if (ctx.chance(4)) actor.unk17E4.unk1 else ctx.int(u8);

    return .{actor};
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    const tick = v.g.vs_gametime_tickspeed().*;
    const half: i32 = @divTrunc(tick, 2);
    if (actor.unk17E4.unk0 != 1 or actor.unk17E4.unk3 == 0) return 0;
    const d: i32 = actor.unk17E4.unk3;
    if (d == half) return 1;
    if (d < half) return 2;
    return 3;
}
