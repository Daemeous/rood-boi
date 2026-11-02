//! zig_diff spec: func_800B770C -> func_800B770C.func_800B770C (4A0A8.c).
//! Inputs: a 16-byte buffer in the arena whose byte 3 and byte 4 are random
//! (byte 4 is biased toward 0x00, 0x0A and 0xFF so each of bits 1 and 3 is
//! set and clear often), and a random short. The four bit classes of arg0[4]
//! (bit 1 and bit 3) each occur. The scratchpad-side callees are stubs: the
//! object init writes sp10, the iterator advances it and ends the loop with
//! a 1 about a quarter of the time, and func_800A0BE0 returns random values
//! so both the 0x800 and the 0x1000 tests go each way.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B770C));

pub const classes = [_][:0]const u8{ "bit1-clear-bit3-clear", "bit1-set-bit3-clear", "bit1-clear-bit3-set", "bit1-set-bit3-set" };

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }

    pub fn func_800BB68C(ctx: *zd.Ctx, id: anytype, out: anytype) void {
        _ = id;
        const o: *v.func_800BB68C_t = @ptrCast(@alignCast(out));
        o.unk0 = ctx.value(u8);
        o.unk1 = ctx.value(u8);
        o.unk2[0] = ctx.value(u8);
    }

    pub fn func_800BB788(ctx: *zd.Ctx, p: anytype) c.u_short {
        const o: *v.func_800BB68C_t = @ptrCast(@alignCast(p));
        o.unk0 = ctx.value(u8);
        // The loop ends with a 1 about a quarter of the time.
        if (ctx.value(u8) < 64) return 1;
        return ctx.value(c.u_short);
    }

    pub fn func_800A0BE0(ctx: *zd.Ctx, id: anytype) c_uint {
        _ = id;
        return ctx.value(c_uint);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 1);
    buf[4] = ctx.pick(u8, &.{ 0x00, 0x0A, 0xFF });
    return .{ @as([*c]u8, buf), ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const bits = args[0][4];
    const bit1 = (bits & 2) != 0;
    const bit3 = (bits & 8) != 0;
    return @as(usize, @intFromBool(bit1)) + 2 * @as(usize, @intFromBool(bit3));
}
