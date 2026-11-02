//! zig_diff spec: func_800BB554 -> func_800BB554.func_800BB554 (4A0A8.c).
//! Inputs: a 16-byte arena buffer. Byte 1 is the index into the 5-entry
//! D_800F4C38 table (0..4, the only indices the harness owns). Bytes 2..3
//! hold the short that vs_battle_getShort reads and returns, so the stub
//! reads it from memory as the real callee does: any i16, negative offsets
//! point before the buffer and are only computed, never dereferenced.
//! The C has no branches and no comparisons; its arguments, the table store,
//! the getShort call and the psx_addr result are what the spec checks.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB554));

pub const classes = [_][:0]const u8{ "offset-negative", "offset-zero", "offset-positive" };

pub const stubs = struct {
    /// The real callee reads the short at its argument; it changes nothing.
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = ctx;
        const b: [*]const u8 = @ptrCast(p);
        return @bitCast([2]u8{ b[0], b[1] });
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 16);
    buf[1] = ctx.edge(u8, &.{ 0, 1, 2, 3, 4 });
    const off = ctx.pick(i16, &.{ -1, -2, 0, 1, 4, 32767, -32768 });
    std.mem.writeInt(i16, buf[2..4], off, .little);
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const p: [*]const u8 = @ptrCast(args[0]);
    const off = std.mem.readInt(i16, p[2..4], .little);
    if (off < 0) return 0;
    return if (off == 0) 1 else 2;
}
