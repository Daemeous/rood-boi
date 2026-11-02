//! zig_diff spec: func_800B6868 -> func_800B6868.func_800B6868 (4A0A8.c).
//! Inputs: the flag D_800F4C2C at 2 (the call path) and at values around it
//! (0, 1, 3, 0x42, 0x82 and 0xFF: the signed char reads -126 and -1 there),
//! arg0 in the arena, in the scratchpad or at PS1 address 0x80004000, and a
//! random arg1 that the C never reads. vs_battle_getShort returns any i16, so
//! the index runs from -32768 to 32767 around arg0. The arena base sits at
//! 40000, so an arena index always stays inside the arena. The C only
//! computes the address, never dereferences it, so a scratchpad or low-RAM
//! base with an out-of-range index is still safe. func_800B6778 may write
//! arg0[0] and returns nothing; the C reads nothing back from it.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B6868));

pub const classes = [_][:0]const u8{ "flag-two", "flag-other" };

pub const stubs = struct {
    pub fn func_800B6778(ctx: *zd.Ctx, p: anytype) void {
        const q: [*]u8 = @ptrCast(p);
        if (ctx.value(u1) == 1) q[0] = ctx.value(u8);
    }

    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F4C2C().* = ctx.pick(u8, &.{ 2, 2, 0, 1, 3, 0x42, 0x82, 0xFF });
    const arg0: [*c]u8 = if (ctx.chance(4))
        @ptrFromInt(zd.scratchpad + 0x100)
    else if (ctx.chance(4))
        @ptrFromInt(zd.low_ram + 0x4000)
    else
        @ptrCast(&zd.zdiff_arena[40000]);
    return .{ arg0, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4C2C().* == 2) 0 else 1;
}
