//! zig_diff spec: func_800A7524 -> func_800A7524.func_800A7524 (3A1A0.c).
//! Inputs: an actor whose 0x1000 flag (unk5AC_12) is set or clear, and a
//! non-null motion vector in every run (the C reads it only with the flag
//! set, but a mutated flag test must not crash the copy instead of differing).
//! motion.vx/vz come from edge values (0, +-1, +-16, extremes) and random
//! ones. The collision scratchpad's eight contact bytes are all zero, or each
//! nonzero half the time, so the summed offsets land on the +-16 clamps and
//! on the in-range side.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A7524));

pub const classes = [_][:0]const u8{ "flag-clear", "no-contacts", "in-range", "clamped" };

fn scratch() *v.collisionResponseScratch {
    return @ptrFromInt(zd.scratchpad + 0x3BC);
}

/// The C's unclamped offsets from the contact bytes (x, then z).
fn sums(k: [8]u8) [2]i32 {
    var b: [8]i32 = undefined;
    for (k, 0..) |byte, i| b[i] = @intFromBool(byte != 0);
    const x = 8 * b[1] + 16 * b[2] + 8 * b[3] - 8 * b[5] - 16 * b[6] - 8 * b[7];
    const z = 16 * b[0] + 8 * b[1] - 8 * b[3] - 16 * b[4] - 8 * b[5] + 8 * b[7];
    return .{ x, z };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const flagSet = !ctx.chance(4);
    psx.setBits(actor, "unk5AC_12", @intFromBool(flagSet));

    const m = ctx.alloc(v.SVECTOR);
    m.vx = ctx.pick(i16, &.{ 0, 1, -1, 16, -16, 32767, -32768 });
    m.vz = ctx.pick(i16, &.{ 0, 1, -1, 16, -16, 32767, -32768 });

    // Contact bytes in the scratchpad: a quarter of the time all clear,
    // otherwise each one set half the time.
    const s = scratch();
    const mode = ctx.range(u8, 0, 3);
    for (&s.contacts) |*k| {
        k.* = if (mode == 0 or ctx.chance(2)) 0 else ctx.pick(u8, &.{ 1, 0x80, 0xFF });
    }

    return .{ actor, m };
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    if (psx.getBits(actor, "unk5AC_12") == 0) return 0;
    const s = scratch();
    if (std.mem.allEqual(u8, &s.contacts, 0)) return 1;
    const xz = sums(s.contacts);
    if (@abs(xz[0]) > 16 or @abs(xz[1]) > 16) return 3;
    return 2;
}
