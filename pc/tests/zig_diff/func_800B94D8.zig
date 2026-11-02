//! zig_diff spec: func_800B94D8 -> func_800B94D8.func_800B94D8 (4A0A8.c).
//! Inputs: an 8-byte record in the arena. Byte 1 is zero about one time in
//! twenty (the else branch) and otherwise an edge value 1, 2, 0x80, 0xFF or
//! random, so the `!= 0` test and the `> 1` boundary both see their edges.
//! Bytes 2 and 3 are the table indices, kept in range (D_800E9BF8 has 4
//! entries, D_800E9BF4 has 2) because the C never checks them. Bytes 4-6 are
//! edge values including sign-bit bytes, so the shifted word goes negative.
//! The table entries are set to edge i16 values (negative ones included) and
//! arg1 is random, so the callee sees signed values from both tables.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B94D8));

pub const classes = [_][:0]const u8{ "else-branch", "call-sign-bit", "call-no-sign-bit" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(8, 1);
    buf[1] = if (ctx.chance(20)) 0 else ctx.pick(u8, &.{ 1, 2, 0x80, 0xFF });
    buf[2] = ctx.range(u8, 0, 3);
    buf[3] = ctx.range(u8, 0, 1);
    buf[4] = ctx.pick(u8, &.{ 0, 0x01, 0x7F, 0x80, 0xFF });
    buf[5] = ctx.pick(u8, &.{ 0, 0x01, 0x7F, 0x80, 0xFF });
    buf[6] = ctx.pick(u8, &.{ 0, 0x01, 0x7F, 0x80, 0xFF });

    const t4 = v.g.D_800E9BF4();
    t4[0] = ctx.pick(i16, &.{ 0, -1, 1, 32767, -32768 });
    t4[1] = ctx.pick(i16, &.{ 0, -1, 1, 32767, -32768 });

    const t8 = v.g.D_800E9BF8();
    for (0..4) |i| {
        t8[i] = ctx.pick(i16, &.{ 0, -1, 1, 32767, -32768 });
    }

    const arg1 = ctx.int(i16);
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    const b = args[0];
    if (b[1] == 0) return 0;
    return if ((b[4] & 0x80) != 0) 1 else 2;
}
