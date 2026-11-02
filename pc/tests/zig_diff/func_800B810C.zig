//! zig_diff spec: func_800B810C -> func_800B810C.func_800B810C (4A0A8.c).
//! Inputs: an 8-byte opcode. prepare picks the mask getShort(script + 3)
//! gives: bit 0 clear or set (the func_8007D03C test), sometimes with the
//! sign bit (the C's short mask is sign-extended before the u_short &), from
//! edges (0, 1, 2, 3, 0x81, 0x8001, 0xFFFF, 0x7FFE) or any value.
//! func_800A0BE0's flags come from edges (0, 1, 0x80, 0x81, 0x7E, 0xFFFF,
//! 0x10000, 0xFFFF0000) or any value, so the ^ 0x81, the | 1 and the u_short
//! narrowing all decide the result; func_8007D03C returns 0, 1 or any value.
//! func_800BFE50's actor carries bits above 16 a third of the time (the C's
//! & 0xFFFF drops them). Classes: mask bit 0 clear; set with the
//! func_8007D03C 0 result; set otherwise.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B810C));

pub const classes = [_][:0]const u8{ "bit0-clear", "bit0-set-d03c-zero", "bit0-set-d03c-nonzero" };

var script: [*]u8 = undefined;
var mask: i16 = 0;
var d03c_zero: bool = false;

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        const at: [*]u8 = @ptrCast(p);
        if (at == script + 3) return mask;
        return ctx.value(c_short);
    }

    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        const actor: c_int = ctx.value(u16);
        return if (ctx.value(u8) % 3 == 0) actor | (@as(c_int, ctx.value(u8) | 1) << 16) else actor;
    }

    pub fn func_800A0BE0(ctx: *zd.Ctx, id: anytype) c_uint {
        _ = id;
        if (ctx.value(bool)) return ctx.value(c_uint);
        const edges = [_]c_uint{ 0, 1, 0x80, 0x81, 0x7E, 0xFFFF, 0x10000, 0xFFFF0000 };
        return edges[ctx.value(u8) % edges.len];
    }

    pub fn func_8007D03C(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        if (d03c_zero) return 0;
        return if (ctx.value(bool)) 1 else ctx.value(c_int) | 1;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    script = ctx.allocBytes(8, 1);
    mask = if (ctx.chance(2)) ctx.edge(i16, &.{ 0, 1, 2, 3, 0x81, @bitCast(@as(u16, 0x8001)), -1, 0x7FFE }) else ctx.int(i16);
    d03c_zero = ctx.chance(2);
    return .{@as([*c]u8, @ptrCast(script))};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (mask & 1 == 0) return 0;
    return if (d03c_zero) 1 else 2;
}
