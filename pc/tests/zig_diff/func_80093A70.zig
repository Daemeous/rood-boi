//! zig_diff spec: func_80093A70 -> func_80093A70.func_80093A70 (2842C.c).
//! Inputs: the i16 global D_800F2258 (-1 a third of the time, otherwise
//! 0, 1, -2, 0x200 and the i16 extremes, which the C sign-extends into its
//! u_int argument) and the 4-byte guest pointer vs_battle_roomData.section14.
//! That pointer is what the C tests and passes on: zero (null), an address
//! in the PS1 low RAM window at 0x80000000 (sign bit set), or a u32 object in
//! the arena (sign bit clear), so the C never follows a bogus address. The
//! callee func_800462E8 is a void recording stub: it returns nothing and the
//! C reads no global or object after the call, so it has nothing to write back.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80093A70));

pub const classes = [_][:0]const u8{ "outer-off", "section14-zero", "section14-high-bit", "section14-low-bits" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const sec = &v.g.vs_battle_roomData().section14;
    v.g.D_800F2258().* = if (ctx.chance(3))
        -1
    else
        ctx.pick(i16, &.{ 0, 1, -2, 0x200, -32768, 32767 });
    switch (ctx.range(u8, 0, 2)) {
        0 => sec.raw = 0,
        1 => sec.raw = 0x8000_0000 + ctx.range(u32, 0, 0x7FFC),
        else => {
            const obj = ctx.alloc(u32);
            obj.* = ctx.int(u32);
            psx.put(sec, obj);
        },
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800F2258().* == -1) return 0;
    const raw = v.g.vs_battle_roomData().section14.raw;
    if (raw == 0) return 1;
    return if (raw & 0x8000_0000 != 0) 2 else 3;
}
