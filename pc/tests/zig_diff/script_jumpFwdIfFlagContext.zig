//! zig_diff spec: vs_battle_script_jumpFwdIfFlagContext -> script_jump_fwd_if_flag_context.script_jumpFwdIfFlagContext (4A0A8.c).
//! Inputs: a 64-byte arena buffer whose byte 1 is compared with the global D_800F4BB4
//! (equal about half the time, so both branches run), and any c_short argument, which the
//! C never reads. The getShort stub returns 0, -1, 0x7FFF or any 16-bit value, so the
//! offset is negative or large and the unsigned-index slip shows. The stub writes nothing:
//! the real getShort only reads the bytes at arg0 + 2.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_jumpFwdIfFlagContext));

pub const classes = [_][:0]const u8{ "unequal", "equal" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(64, 1);
    const flag = v.g.D_800F4BB4();
    flag.* = if (ctx.chance(2)) buf[1] else ctx.int(u8);
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const b = args[0];
    if (v.g.D_800F4BB4().* == b[1]) return 1;
    return 0;
}

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return switch (ctx.value(u8) % 4) {
            0 => 0,
            1 => -1,
            2 => 0x7fff,
            else => @bitCast(ctx.value(u16)),
        };
    }
};
