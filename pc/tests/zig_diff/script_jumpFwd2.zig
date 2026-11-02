//! zig_diff spec: vs_battle_script_jumpFwd2 -> script_jump_fwd2.script_jumpFwd2 (4A0A8.c).
//! Inputs: arg0 anywhere the harness owns (the arena, the scratchpad, or PS1 RAM
//! at address 0), and arg1 any short (the C never reads it). The C reads no memory
//! itself: vs_battle_getShort is a recording stub that returns any i16, so the
//! index is negative about half the time and the returned address wraps.
const std = @import("std");
const zd = @import("zdiff");
const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_jumpFwd2));

pub const Args_ = Args;

pub const classes = [_][:0]const u8{ "arena", "scratchpad", "low-ram" };

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const which = ctx.range(u8, 0, 2);
    const p: [*c]u8 = switch (which) {
        0 => @ptrCast(&zd.zdiff_arena[ctx.range(usize, 0, zd.arena_size - 1)]),
        1 => @ptrFromInt(zd.scratchpad + ctx.range(usize, 0, zd.scratchpad_size - 1)),
        else => @ptrFromInt(zd.low_ram + ctx.range(usize, 0, zd.low_ram_size - 1)),
    };
    return .{ p, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const a = @intFromPtr(args[0]);
    const arena: usize = @intFromPtr(&zd.zdiff_arena[0]);
    if (a >= arena and a - arena < zd.arena_size) return 0;
    if (a >= zd.scratchpad and a - zd.scratchpad < zd.scratchpad_size) return 1;
    return 2;
}
