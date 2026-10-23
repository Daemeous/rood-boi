//! zig_diff spec: func_800938AC -> func_800938AC.func_800938AC (2842C.c).
//! Inputs: arg0 as 0, +/-1, 0x100 and the int extremes (both sides of the
//! `!= 0` test, negative values included); D_800F2258 as -1 (gate closed) or
//! an i16 edge; section14 either null (no call) or the guest address of an
//! arena block that the harness can read, so every call gets a valid pointer.
//! The callee func_80046494 is a recording stub. It changes nothing the C
//! reads back (the C reads nothing after the call), so it needs no behaviour.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800938AC));

pub const classes = [_][:0]const u8{ "gate-closed", "null-section14", "arg-nonzero", "arg-zero" };

fn section14Raw() *align(4) u32 {
    return @ptrCast(@alignCast(&v.g.vs_battle_roomData().section14));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const d = v.g.D_800F2258();
    if (ctx.chance(3)) d.* = -1 else d.* = ctx.pick(i16, &.{ 0, 1, -2, 32767, -32768 });

    // section14 is a guest pointer the C dereferences: null, or a block the
    // harness owns (its address is a real guest address the call can read).
    const raw = section14Raw();
    if (ctx.chance(4)) {
        raw.* = 0;
    } else {
        const block = ctx.alloc([16]u8);
        raw.* = psx.addr(block);
    }

    const arg0 = ctx.pick(c_int, &.{ 0, 1, -1, 0x100, std.math.minInt(c_int), std.math.maxInt(c_int) });
    return .{arg0};
}

pub fn classify(args: Args) usize {
    if (v.g.D_800F2258().* == -1) return 0;
    if (section14Raw().* == 0) return 1;
    return if (args[0] != 0) 2 else 3;
}
