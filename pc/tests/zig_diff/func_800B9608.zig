//! zig_diff spec: func_800B9608 -> func_800B9608.func_800B9608 (4A0A8.c).
//! Inputs: a 16-byte buffer in the arena whose byte 3 is 0xFE a third of the
//! time and another selector value otherwise (0xFD, 0xFF, 0x00, 0x7F, 0x80), with
//! bytes 1 and 5 random so the getShort argument and the second argument of
//! func_8007CA20 vary. The short arg1 is unused by the C but varied.
//! getShort returns any short; func_800BFE50 returns any 32-bit int, so the
//! truncation of its result to u_short and the sign of the value passed on
//! are both visible to the port.
const std = @import("std");
const zd = @import("zdiff");
const zdiffPort = @import("port");

pub const Args = std.meta.ArgsTuple(@TypeOf(zdiffPort.func_800B9608));

pub const classes = [_][:0]const u8{ "selector-fe", "selector-other" };

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }

    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = ctx.allocBytes(16, 1);
    if (ctx.chance(3)) {
        buf[3] = 0xFE;
    } else {
        buf[3] = ctx.pick(u8, &.{ 0xFD, 0xFF, 0x00, 0x7F, 0x80 });
    }
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[0][3] == 0xFE) 0 else 1;
}
