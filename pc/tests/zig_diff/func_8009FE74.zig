//! zig_diff spec: func_8009FE74 -> func_8009FE74.func_8009FE74 (30DB0.c).
//! Inputs: an index 0..16 into D_800F4538 and a selector arg1 drawn from the
//! edges -1, 0, 1, 6, 7, 8, -2 and any random int. The index's object comes
//! from D_800F4538 (the main path), from D_800F45E0 (the fallback path, index
//! below 16), or from neither (the -1 return). Index 16 always takes the main
//! path, since D_800F45E0 has only 16 entries. The C makes no calls, so there
//! are no stubs. The state bytes of the chosen object (offset 0x64) are random,
//! so the masks and the clear show up against prior bits.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009FE74));

pub const classes = [_][:0]const u8{ "no-object", "clear-table", "clear-fallback", "mask-table", "mask-fallback", "bad-bit" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const t = v.g.D_800F4538();
    const f = v.g.D_800F45E0();
    const idx = ctx.range(c_int, 0, 16);
    const i: usize = @intCast(idx);
    // Index 16 has no D_800F45E0 entry, so it always takes the main path.
    const mode: u8 = if (i == 16) 0 else ctx.range(u8, 0, 2);
    const obj = ctx.alloc(v.D_800F4538_t);
    const alt = ctx.alloc(v.D_800F45E0_t);
    t[i] = if (mode == 0) obj else null;
    if (i < 16) f[i] = if (mode == 1 or (mode == 0 and ctx.chance(2))) alt else null;
    // 7 appears twice, so arg1 == 7 (the equal side of `arg1 > 7u`) is common.
    const bit = ctx.pick(c_int, &.{ -1, 0, 1, 6, 7, 7, 8, -2 });
    return .{ idx, bit };
}

pub fn classify(args: Args) usize {
    const i: usize = @intCast(args[0]);
    const table = v.g.D_800F4538()[i] != null;
    if (!table and (i == 16 or v.g.D_800F45E0()[i] == null)) return 0;
    if (args[1] == -1) return if (table) 1 else 2;
    if (@as(u32, @bitCast(args[1])) > 7) return 5;
    return if (table) 3 else 4;
}
