//! zig_diff spec: func_800A46A4 -> func_800A46A4.func_800A46A4 (3A1A0.c).
//! Inputs: one actor in the arena. unk63C (the radius) is 0, 1, 4095, 4096, 0xFFFF
//! or any u16; unk1800 (the heading) is 0, 511, 512, -1, -512, -32768, 32767 or any
//! i16, so its sign bit selects the (u_short) wrap. The callee rsin returns an
//! i16-range value, so radius * rsin stays inside i32 and the division sees both
//! signs. The callee func_800A70DC rewrites unk1800 and the eight contact bytes
//! at 0x1F8003E8 in one of four patterns (all clear, random, only the last
//! nonzero, only the first nonzero with bit 7 set). That covers the no-contact
//! exit of the loop (direction == 8), the contact found inside the loop, and
//! contact values with and without the sign bit.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A46A4));

pub const classes = [_][:0]const u8{ "radius-zero", "heading-negative", "heading-positive" };

pub const stubs = struct {
    pub fn rsin(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return @as(c_int, ctx.value(i16));
    }

    pub fn func_800A70DC(ctx: *zd.Ctx, actor: anytype, amount: anytype) void {
        _ = amount;
        const a: *v.D_800F4538_t = @ptrCast(@alignCast(actor));
        a.unk1800 = ctx.value(i16);
        // The scratchpad contact bytes: collisionScratchView at 0x1F8003BC, contact at +44.
        const contact: [*]u8 = @ptrFromInt(0x1F8003E8);
        const mode = ctx.value(u2);
        var i: usize = 0;
        while (i < 8) : (i += 1) {
            contact[i] = switch (mode) {
                0 => 0,
                1 => ctx.value(u8),
                2 => if (i == 7) ctx.value(u8) | 1 else 0,
                3 => if (i == 0) ctx.value(u8) | 0x80 else 0,
            };
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    actor.unk63C = ctx.pick(u16, &.{ 0, 1, 4095, 4096, 0xFFFF });
    actor.unk1800 = ctx.pick(i16, &.{ 0, 511, 512, -1, -512, -32768, 32767 });
    return .{actor};
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    if (actor.unk63C == 0) return 0;
    if (actor.unk1800 < 0) return 1;
    return 2;
}
