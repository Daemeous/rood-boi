//! zig_diff spec: func_800B8590 -> func_800B8590.func_800B8590 (4A0A8.c).
//! Inputs: a 16-byte arena header whose byte 9 (the count) is zero (the
//! default 0x1000 path), a low nonzero count, or a count with the sign bit
//! set (0x80..0xFF, read as unsigned). The header's other bytes are random,
//! as is arg1, which the C never reads. The three vector shorts, the lookup
//! key and the lookup result come from the stubs, so the vector and the
//! count reach func_8009FC60 as arbitrary values. func_8009FC60 copies the
//! vector's vx, vy and vz into an arena log (the C leaves pad unset), so a
//! wrong or swapped field shows in the state compare.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B8590));

/// Set by prepare: the arena log func_8009FC60 copies the vector to.
var log: *[3]i16 = undefined;

pub const classes = [_][:0]const u8{ "count-zero", "count-low", "count-high-bit" };

pub const stubs = struct {
    /// Reads a header field: any short, never touches memory.
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }
    /// The lookup: any int, the value the C passes on as temp_v0.
    pub fn func_800BFE50(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        return ctx.value(c_int);
    }
    /// Takes the vector and the count; logs vx, vy, vz.
    /// Returns an int, as its C prototype does.
    pub fn func_8009FC60(ctx: *zd.Ctx, a: anytype, b: anytype, vec: anytype, n: anytype) c_int {
        _ = a;
        _ = b;
        _ = n;
        const sv: *v.SVECTOR = @ptrCast(@alignCast(vec));
        log.* = .{ sv.vx, sv.vy, sv.vz };
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    log = ctx.alloc([3]i16);
    const buf: [*]u8 = ctx.allocBytes(16, 16);
    buf[9] = if (ctx.chance(3)) 0 else ctx.edge(u8, &.{ 1, 0x7f, 0x80, 0xff, 2, 0x81 });
    return .{ @as([*c]u8, @ptrCast(buf)), ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const n = args[0][9];
    if (n == 0) return 0;
    return if (n & 0x80 != 0) 2 else 1;
}
