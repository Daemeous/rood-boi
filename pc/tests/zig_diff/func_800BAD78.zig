//! zig_diff spec: func_800BAD78 -> func_800BAD78.func_800BAD78 (4A0A8.c).
//! Inputs: a 4-byte arena buffer as arg0 (arg0[1..2] feed the getShort stub,
//! arg0[3] is passed to func_8007D08C on every step) and any arg1 (the C never
//! reads it). The iterator stub stops at once, after a few steps, or only after
//! many: that is the three classes. The first iterator call always returns 0
//! (not the break), so a loop that breaks on 0 is seen at once, and the later
//! calls return 1 (the loop's break) or any u16 (0 and 0xFFFF included), with a
//! hard bound so the C always terminates. func_800BB68C and func_800BB788 write
//! the out-struct the C passes them; func_8007D08C returns any int.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BAD78));

pub const classes = [_][:0]const u8{ "break-first", "short-loop", "long-loop" };

/// Loop shape of this iteration: set by prepare, read by the stubs and classify.
var mode: u8 = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    mode = ctx.edge(u8, &.{ 0, 1, 2 });
    const buf = ctx.allocBytes(4, 1);
    buf[3] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    return .{ buf, ctx.pick(c_short, &.{ 0, -1, 32767, -32768 }) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return mode;
}

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }

    pub fn func_800BB68C(ctx: *zd.Ctx, id: anytype, out: anytype) void {
        _ = id;
        const s: *v.func_800BB68C_t = @ptrCast(@alignCast(out));
        s.unk0 = ctx.value(u8);
        s.unk1 = ctx.value(u8);
    }

    pub fn func_800BB788(ctx: *zd.Ctx, out: anytype) u16 {
        // The iterator advances the out-struct the next call reads.
        const s: *v.func_800BB68C_t = @ptrCast(@alignCast(out));
        s.unk0 = ctx.value(u8);
        // The first iterator call (the early stub calls of this run) returns 0,
        // which is not the break: a loop that breaks on 0 stops here instead.
        if (ctx.calls < 4) return 0;
        // Bound the loop: after 60 stub calls it always stops.
        if (ctx.calls > 60) return 1;
        return switch (mode) {
            0 => 1,
            1 => if (ctx.value(u8) < 128) 1 else ctx.value(u16),
            else => if (ctx.value(u8) < 8) 1 else ctx.value(u16),
        };
    }

    pub fn func_8007D08C(ctx: *zd.Ctx, step: anytype, flag: anytype) c_int {
        _ = step;
        _ = flag;
        return ctx.value(c_int);
    }
};
