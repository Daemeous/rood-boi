//! zig_diff spec: func_800B7014 -> func_800B7014.func_800B7014 (4A0A8.c).
//! Inputs: arg0 is a six-byte block in the arena. Byte 1's top two bits pick
//! each of the four D_800F4B30 and D_800EB9BC entries, and bytes 2-5 take the
//! signed-char and shift edges (0x00, 0x7F, 0x80, 0xFF). arg1 is any short and
//! unused by the C. The D_800EB9BC pointer is NULL one time in three, otherwise
//! it points at an arena table of four entries. func_800C8550 is a void callee
//! whose effects the C never reads back, so it needs no stub: the state hash
//! taken at the call is what a port must match.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B7014));

pub const classes = [_][:0]const u8{ "null-table", "sign-bit-set", "sign-bit-clear" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800EB9BC();
    if (ctx.chance(3)) {
        table.* = null;
    } else {
        // Four entries: byte 1 >> 6 reaches 0..3.
        const obj: *v.D_800EB9BC_t = @ptrCast(@alignCast(ctx.allocBytes(4 * @sizeOf(v.D_800EB9BC_t), 16)));
        table.* = obj;
    }

    const buf = ctx.allocBytes(8, 16);
    buf[0] = ctx.int(u8);
    buf[1] = ctx.int(u8);
    buf[2] = ctx.pick(u8, &.{ 0x00, 0x7F, 0x80, 0xFF });
    buf[3] = ctx.pick(u8, &.{ 0x00, 0x7F, 0x80, 0xFF });
    buf[4] = ctx.pick(u8, &.{ 0x00, 0x01, 0x02, 0x7F, 0x80, 0xFF });
    buf[5] = ctx.pick(u8, &.{ 0x00, 0x01, 0x7F, 0x80, 0xFF });
    buf[6] = ctx.int(u8);
    buf[7] = ctx.int(u8);

    const arg0: [*c]u8 = buf;
    return .{ arg0, ctx.int(i16) };
}

pub fn classify(args: Args) usize {
    if (v.g.D_800EB9BC().* == null) return 0;
    return if (args[0][2] & 0x80 != 0) 1 else 2;
}
