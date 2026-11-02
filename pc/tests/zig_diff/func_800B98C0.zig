//! zig_diff spec: func_800B98C0 -> func_800B98C0.func_800B98C0 (4A0A8.c).
//! Inputs: a 16-byte arena block standing for arg0, with byte 1 zero half the
//! time (the C's door-section test) and the other bytes random, so bit 2 and
//! bit 3 of byte 7 vary independently. D_800F4BE2 takes the values that reach
//! each branch: 0 (run the block), 2 (return 0 and reset), or any other value
//! (return 1), with negative and high values among them. arg1 is random and
//! unused. The callees are recording stubs: they write the fields the C
//! stores into sp10 into the arena, and sometimes change arg0[7], which the C
//! re-reads after the calls.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B98C0));

pub const classes = [_][:0]const u8{ "done-2", "skipped-other", "callee-zero", "callee-nonzero" };

/// The arg0 block the current iteration passed in (the stubs write into it).
var g_arg0: [*c]u8 = undefined;

/// Records the door-section fields the C sets, as bytes in the arena, so the
/// comparison sees them. Bits the C never sets are left out (they are stack
/// garbage in both runs).
fn snapshot(ctx: *zd.Ctx, p: anytype) void {
    const s: *v._mpdRoomDoorSection_t = @ptrCast(@alignCast(p.?));
    const out = ctx.allocBytes(8, 1);
    out[0] = s.zndId;
    out[1] = s.mpdId;
    out[2] = @intCast(psx.getBits(&s.unk2, "unk0_13"));
    out[3] = @intCast(psx.getBits(s, "unk4_8"));
    out[4] = @intCast(psx.getBits(s, "unk4_13"));
    out[5] = @intCast(psx.getBits(s, "unk4_15"));
    out[6] = @intCast(psx.getBits(s, "unk6_10"));
    out[7] = @intCast(psx.getBits(s, "unk6_15"));
}

pub const stubs = struct {
    pub fn func_8007BE10(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        if (ctx.value(u1) == 1) g_arg0[7] = ctx.value(u8);
        if (ctx.value(u1) == 1) v.g.D_800F4BE2().* = ctx.value(i16);
        return ctx.value(c_int);
    }

    pub fn func_8007C580(ctx: *zd.Ctx, p: anytype, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        snapshot(ctx, p);
        if (ctx.value(u1) == 1) g_arg0[7] = ctx.value(u8);
        if (ctx.value(u1) == 1) v.g.D_800F4BE2().* = ctx.value(i16);
    }

    pub fn func_8007C654(ctx: *zd.Ctx, p: anytype, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        snapshot(ctx, p);
        if (ctx.value(u1) == 1) g_arg0[7] = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 16);
    g_arg0 = buf;
    buf[1] = if (ctx.chance(2)) 0 else ctx.range(u8, 1, 255);
    v.g.D_800F4BE2().* = ctx.edge(i16, &.{ 0, 0, 1, 2, 2, 3, -1, -32768, -32766, 0x7FFF });
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const d = v.g.D_800F4BE2().*;
    if (d != 0) return if (d == 2) 0 else 1;
    return if (args[0][1] == 0) 2 else 3;
}
