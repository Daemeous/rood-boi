//! zig_diff spec: func_8009E480 -> func_8009E480.func_8009E480 (30DB0.c).
//! Inputs: D_800F4538[0] always points at an arena D_800F4538_t (the C
//! dereferences it without a null test), with animationId drawn from the
//! two compared values 0x63 and 0xC7, their neighbours 0x62 and 0xC6, 0, 0xFF,
//! or any byte. The function has no callees, so no stubs are needed.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009E480));

pub const classes = [_][:0]const u8{ "anim-63", "anim-c7", "anim-other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    const table = v.g.D_800F4538();
    table[0] = obj;
    obj.animationId = ctx.pick(u8, &.{ 0x63, 0xC7, 0x62, 0xC6, 0, 0xFF });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const id = v.g.D_800F4538()[0].?.animationId;
    return switch (id) {
        0x63 => 0,
        0xC7 => 1,
        else => 2,
    };
}
