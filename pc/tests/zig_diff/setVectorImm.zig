//! zig_diff spec: _setVectorImm -> set_vector_imm.setVectorImm (4A0A8.c).
//! Inputs: a VECTOR in the arena and a 16-byte arena block as the source
//! bytes, read at offsets 0-3 so the class is the source's alignment. The
//! three vs_battle_getShort stubs return the extremes (-32768, 32767, 0, -1)
//! a quarter of the time each and any i16 otherwise, so the products span
//! their full range, including negatives and the sign bit. The C has no
//! branches or comparisons; the stubs record the three reads and their order.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setVectorImm));

pub const classes = [_][:0]const u8{ "src-aligned", "src-plus-1", "src-plus-2", "src-plus-3" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const out = ctx.alloc(v.VECTOR);
    const bytes = ctx.allocBytes(16, 16);
    const off = ctx.range(usize, 0, 3);
    return .{ out, @as([*c]u8, bytes + off) };
}

pub fn classify(args: Args) usize {
    return @intFromPtr(args[1]) & 3;
}

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        const r = ctx.value(u8);
        if (r < 32) return -32768;
        if (r < 64) return 32767;
        if (r < 96) return 0;
        if (r < 128) return -1;
        return ctx.value(c_short);
    }
};
