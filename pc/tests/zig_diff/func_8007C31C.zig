//! zig_diff spec: func_8007C31C -> func_8007C31C.func_8007C31C (146C.c).
//! Inputs: vs_battle_characterState is an arena actor (the C dereferences it
//! unconditionally) whose stats word is null, an arena block, or any 32-bit
//! value (the C passes it on without reading it).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8007C31C));

pub const classes = [_][:0]const u8{ "null-stats", "arena-stats", "any-stats" };

var mode: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.vs_battle_actor);
    v.g.vs_battle_characterState().* = actor;
    mode = ctx.range(usize, 0, 2);
    actor.stats.raw = switch (mode) {
        0 => 0,
        1 => psx.addr(ctx.alloc(v.vs_battle_actor2)),
        else => ctx.int(u32) & ~@as(u32, 3), // psx.get: a guest pointer is 4-aligned
    };
    return .{};
}

pub fn classify(_: Args) usize {
    return mode;
}
