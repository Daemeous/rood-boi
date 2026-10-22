//! zig_diff spec: vs_battle_getShort -> get_short.getShort (573B8.c).
//! Inputs: a pointer into the arena or into PS1 RAM at address 0 (psx.lowRam),
//! at an even or an odd address. The even path reads a short; the odd path
//! assembles two bytes, so both branches run on both memory regions. The
//! lowRam addresses (0x80000000 + n) have bit 31 set, so the `& 1` test sees
//! values with and without the sign bit of the 32-bit long. The C makes no
//! calls, so there are no stubs. Random bytes fill both regions, so the high
//! byte of the result varies and the short's sign bit is set about half the time.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").getShort));

pub const classes = [_][:0]const u8{ "arena-even", "arena-odd", "lowram-even", "lowram-odd" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const odd = ctx.chance(2);
    const lowram = ctx.chance(3);
    const off: usize = ctx.range(usize, 0, if (lowram) zd.low_ram_size - 8 else zd.arena_size - 8);
    const base: usize = if (lowram) zd.low_ram else @intFromPtr(&zd.zdiff_arena);
    var addr = (base + off) & ~@as(usize, 1);
    if (odd) addr += 1;
    return .{@ptrFromInt(addr)};
}

pub fn classify(args: Args) usize {
    const a = @intFromPtr(args[0]);
    const low = a >= zd.low_ram and a < zd.low_ram + zd.low_ram_size;
    return (if (low) @as(usize, 2) else 0) + (a & 1);
}
