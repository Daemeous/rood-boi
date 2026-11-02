//! zig_diff spec: func_800B7F84 -> func_800B7F84.func_800B7F84 (4A0A8.c).
//! Inputs: a random 8-byte command buffer in the arena (bytes 3-5 are the
//! bitfield source, byte 6 selects the `!= 0xFF` branch, drawn from edges
//! 0xFF, 0xFE, 0x80, 0x7F, 0 and 1) and a random short. The index comes from
//! the stubbed func_800BFE50 in 0..23, the size of D_800F4B70, so every
//! store stays in memory the harness owns. func_800A9CDC's stub reads the
//! struct and the table entry the C has just stored, and writes table
//! entries the C reads back, so store order and field contents show up.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B7F84));

pub const classes = [_][:0]const u8{ "byte6-ff", "byte6-other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(8, 1);
    buf[6] = ctx.pick(u8, &.{ 0xFF, 0xFE, 0x80, 0x7F, 0, 1 });
    const p: [*c]u8 = buf;
    return .{ p, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return if (args[0][6] == 0xFF) 0 else 1;
}

pub const stubs = struct {
    /// Index in 0..23 so the C's table store stays inside D_800F4B70.
    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return @intCast(ctx.value(u8) % 24);
    }

    /// Reads the struct and the table entry at idx (so the C's store to it
    /// must come first), then writes two table entries the C reads back.
    pub fn func_800A9CDC(ctx: *zd.Ctx, idx: anytype, sp: anytype, mode: anytype) void {
        _ = mode;
        const tbl = v.g.D_800F4B70();
        const i: usize = @intCast(idx);
        const p: *v.func_8006EBF8_t_fields = @ptrCast(@alignCast(sp));
        const sum: u8 = psx.getBits(p, "unk0_0") +% psx.getBits(p, "unk0_8") +% psx.getBits(p, "unk0_16") +% tbl[i];
        tbl[(i + 1) % 24] = sum;
        tbl[(i + 5) % 24] = ctx.value(u8);
    }
};
