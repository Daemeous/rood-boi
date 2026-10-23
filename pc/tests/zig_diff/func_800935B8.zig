//! zig_diff spec: func_800935B8 -> func_800935B8.func_800935B8 (2842C.c).
//! Inputs: section14 of vs_battle_roomData is null, an arena object, or a
//! guest address in the PS1 low RAM (raw value 0x80000000 + offset, so its sign
//! bit is set). arg1's unk4[0].u16[1] takes edge values around 0x7FFF/0x8000,
//! and arg0 is sometimes null (the C never reads it). The callee stub may
//! write D_800F2258 and the int that arg1 points to.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800935B8));

pub const classes = [_][:0]const u8{ "section14-null", "section14-arena", "section14-lowram" };

pub const stubs = struct {
    pub fn func_8004644C(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype) void {
        _ = arg0;
        _ = arg2;
        if (ctx.value(u1) == 1) v.g.D_800F2258().* = ctx.value(i16);
        if (@intFromPtr(arg1) != 0 and ctx.value(u1) == 1) {
            const t: *align(1) c_int = @ptrCast(arg1);
            t.* = ctx.value(c_int);
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const room = v.g.vs_battle_roomData();

    const out = ctx.alloc(v.func_80092F74_t2);
    out.unk4[0].u16[1] = ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFF });

    const arg0: ?*v.func_80092F74_t = if (ctx.chance(2)) null else ctx.alloc(v.func_80092F74_t);

    switch (ctx.range(u8, 0, 2)) {
        0 => {
            const none: ?*u32 = null;
            psx.put(&room.section14, none);
        },
        1 => psx.put(&room.section14, ctx.alloc(u32)),
        else => {
            const off: isize = @intCast(4 * ctx.range(u32, 0, 0x1FFF));
            psx.put(&room.section14, psx.byteAt(u32, psx.lowRam(u8), off));
        },
    }

    return .{ arg0, out };
}

pub fn classify(args: Args) usize {
    _ = args;
    const raw = v.g.vs_battle_roomData().section14.raw;
    if (raw == 0) return 0;
    return if (raw & 0x8000_0000 != 0) 2 else 1;
}
