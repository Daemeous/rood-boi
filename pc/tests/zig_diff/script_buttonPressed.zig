//! zig_diff spec: vs_battle_script_buttonPressed -> script_button_pressed.script_buttonPressed (4A0A8.c).
//! Inputs: a 4-byte block in the arena whose byte 1 (the one the C reads) is
//! drawn from edges (0, 0x01, 0x7F, 0x80, 0xFF) or any value, and the global
//! vs_main_buttonsPressed set to edge masks (0, all ones, bit 31, bit 7, bit 8,
//! the high 24 bits) or left random. Both outcomes of the test occur often.
//! arg1 is unused by the C and is drawn at random. The C has no ordered
//! comparison and no null test. Its only value test is on the unsigned result
//! of `all & arg0[1]`; that result is a u32 AND a byte, so bit 31 can never be
//! set and the sign-bit half of that value flag cannot be reached.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_buttonPressed));

pub const classes = [_][:0]const u8{ "no-overlap", "overlap" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 1);
    buf[0] = ctx.int(u8);
    buf[1] = ctx.pick(u8, &.{ 0, 0x01, 0x7F, 0x80, 0xFF });
    buf[2] = ctx.int(u8);
    buf[3] = ctx.int(u8);

    const buttons = v.g.vs_main_buttonsPressed();
    if (ctx.chance(3)) {
        buttons.all = ctx.pick(u32, &.{ 0, 0xFFFF_FFFF, 0x8000_0000, 0x0000_0080, 0x0000_0100, 0xFFFF_FF00 });
    }

    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const overlap = (v.g.vs_main_buttonsPressed().all & args[0][1]) != 0;
    return if (overlap) 1 else 0;
}
