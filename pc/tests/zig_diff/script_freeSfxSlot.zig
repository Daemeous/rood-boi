//! zig_diff spec: vs_battle_script_freeSfxSlot -> script_free_sfx_slot.script_freeSfxSlot (4A0A8.c).
//! Inputs: a 2-byte script buffer in the arena whose second byte (the slot)
//! is zero, has the sign bit set, or is any other value, and an arbitrary
//! short arg1 (edges included; the C never reads it). The callee stub
//! returns 0, INT_MIN, or any int, so both sides of `!= 0` are reached, the
//! sign bit is tested with and without it set, and the slot argument is
//! recorded in the call trace.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_freeSfxSlot));

pub const classes = [_][:0]const u8{ "slot-zero", "slot-sign-bit", "slot-other" };

pub const stubs = struct {
    pub fn vs_main_freeSfxQueueSlot(ctx: *zd.Ctx, slot: anytype) c_int {
        _ = slot;
        const r = ctx.value(u8);
        if (r < 64) return 0;
        if (r < 96) return std.math.minInt(c_int);
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*]u8 = ctx.allocBytes(2, 1);
    buf[1] = ctx.pick(u8, &.{ 0, 0x80, 0xFF, 1, 0x7F });
    return .{ buf, ctx.pick(i16, &.{ 0, -1, 32767, -32768 }) };
}

pub fn classify(args: Args) usize {
    const slot = args[0][1];
    if (slot == 0) return 0;
    if (slot & 0x80 != 0) return 1;
    return 2;
}
