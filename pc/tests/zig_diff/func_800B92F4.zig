//! zig_diff spec: func_800B92F4 -> func_800B92F4.func_800B92F4 (4A0A8.c).
//! Inputs: a 4-byte script in the arena whose byte 1 is the selector (the
//! state cases 10-12, 13, 14, 15 and default, plus 9, 16, 0 and 255), the
//! D_800F4BF8 busy flag zero or not, and random globals and state. The stubs
//! return zero and nonzero (negative included) from func_8007B9FC, an
//! unk0 (a short) of 2, 0x102 (2 in the low byte only) or any other value
//! from func_800CB66C, and func_8007B9A0 sometimes
//! writes D_800F4C69, so a store moved across that call shows. The state
//! flag getter returns 1 about a third of the time, so its equality test
//! against 1 is hit often.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B92F4));

pub const classes = [_][:0]const u8{
    "idle-set",
    "idle-13",
    "idle-14",
    "idle-15",
    "idle-default",
    "busy-set",
    "busy-13",
    "busy-14",
    "busy-15",
    "busy-default",
};

pub const stubs = struct {
    pub fn vs_battle_getStateFlag(ctx: *zd.Ctx, id: anytype) u8 {
        _ = id;
        // Equal to 1 about a third of the time: the C's `== 1` test is then
        // an exact boundary hit, which a port's `<` for `<=` would miss.
        return if (ctx.value(u2) == 0) 1 else ctx.value(u8);
    }

    pub fn func_8007B9A0(ctx: *zd.Ctx) void {
        // The callee may write the flag the C stores after the call.
        if (ctx.value(u1) == 1) v.g.D_800F4C69().* = ctx.value(u8);
    }

    pub fn func_8007B9CC(ctx: *zd.Ctx) void {
        _ = ctx;
    }

    pub fn func_8007B9FC(ctx: *zd.Ctx) c_int {
        if (ctx.value(u2) == 0) return 0;
        const x = ctx.value(c_int);
        return if (x == 0) -1 else x;
    }

    pub fn func_800C97BC(ctx: *zd.Ctx) void {
        _ = ctx;
    }

    pub fn func_800CB114(ctx: *zd.Ctx) void {
        _ = ctx;
    }

    pub fn func_800CB66C(ctx: *zd.Ctx) ?*c.D_800F18EC_t {
        // Always a valid object: the C dereferences it unconditionally.
        // unk0 is 2 about half the time.
        const p = ctx.allocBytes(16, 16);
        const unk0: u16 = switch (ctx.value(u2)) {
            0, 1 => 2,
            2 => 0x102,
            else => ctx.value(u16),
        };
        p[0] = @truncate(unk0);
        p[1] = @truncate(unk0 >> 8);
        p[2] = 0;
        p[3] = 0;
        return @ptrCast(@alignCast(p));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 16);
    buf[0] = ctx.int(u8);
    buf[1] = ctx.edge(u8, &.{ 10, 11, 12, 13, 14, 15, 9, 16, 255, 0 });
    buf[2] = ctx.int(u8);
    buf[3] = ctx.int(u8);
    // Busy about two thirds of the time; zero otherwise.
    v.g.D_800F4BF8().* = if (ctx.chance(3)) 0 else ctx.range(u8, 1, 255);
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const sel = args[0][1];
    const busy: usize = if (v.g.D_800F4BF8().* != 0) 5 else 0;
    const group: usize = switch (sel) {
        10, 11, 12 => 0,
        13 => 1,
        14 => 2,
        15 => 3,
        else => 4,
    };
    return busy + group;
}
