//! zig_diff spec: vs_battle_script_setFlagIfButton -> script_set_flag_if_button.script_setFlagIfButton (4A0A8.c).
//! Inputs: a 4-byte arena block whose bytes 1..3 hold the little-endian short the
//! C reads through vs_battle_getShort (the stub reads it back from memory, so the
//! class is known before the run), with the sign bit clear, set with its low ten
//! bits zero, or set with low bits present. The argument short is unused by the C
//! and random. The global button state is left as the random fill, so the pad
//! byte and the previous state differ between runs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_setFlagIfButton));

pub const classes = [_][:0]const u8{ "sign-clear", "sign-set-low-zero", "sign-set" };

fn shortAt(p: [*]const u8) u16 {
    return std.mem.readInt(u16, p[1..3][0..2], .little);
}

/// The C's getShort reads the short at its argument from memory; the value is
/// whatever prepare wrote there.
pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = ctx;
        const b: [*]const u8 = @ptrCast(p);
        return @bitCast(std.mem.readInt(u16, b[0..2], .little));
    }
    // vs_battle_setStateFlag is a void callee that only records its arguments:
    // the C never reads anything back from it.
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*]u8 = ctx.allocBytes(4, 1);
    const bytes = buf[0..4];
    const raw = ctx.pick(u16, &.{ 0, 0x8000, 0x7FFF, 0x3FF, 0x400, 0xFFFF, 0xFC00, 0x8400 });
    std.mem.writeInt(u16, bytes[1..3][0..2], raw, .little);
    const p: [*c]u8 = @ptrCast(buf);
    return .{ p, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const b: [*]const u8 = @ptrCast(args[0]);
    const raw = shortAt(b);
    if ((raw & 0x8000) == 0) return 0;
    if ((raw & 0x3FF) == 0) return 1;
    return 2;
}
