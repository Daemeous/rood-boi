//! zig_diff spec: func_8009FD38 -> func_8009FD38.func_8009FD38 (30DB0.c).
//! Inputs: one D_800F4538_t in the arena (about 6 KB, so it fits) with a random
//! fill. The C only stores into it, so the only input that matters is unk63C,
//! the u16 compared with 128. Its edges are 0, 127, 128, 129, 0x7FFF, 0x8000
//! and 0xFFFF: 128 is hit often enough for the equal side, and the values at
//! or above 0x8000 are the ones a signed read would see as negative. The
//! initial pad is random, so a dropped or misplaced store shows in the bytes.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009FD38));

pub const classes = [_][:0]const u8{ "below-128", "equal-128", "above-128", "high-bit" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    obj.unk63C = ctx.pick(u16, &.{ 0, 127, 128, 129, 0x7FFF, 0x8000, 0xFFFF });
    obj.unk17F4.pad = ctx.int(i16);
    return .{obj};
}

pub fn classify(args: Args) usize {
    const x = args[0].?.unk63C;
    if (x >= 0x8000) return 3;
    if (x == 128) return 1;
    if (x > 128) return 2;
    return 0;
}
