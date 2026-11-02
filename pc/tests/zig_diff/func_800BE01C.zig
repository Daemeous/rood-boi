//! zig_diff spec: func_800BE01C -> func_800BE01C.func_800BE01C (4A0A8.c).
//! Inputs: a random-state func_800BDF6C_t in the arena whose mode byte
//! (unk0.u8[1]) is 0-4, 0x80 or 0xFF (every switch arm and the default), a
//! step from vs_gametime_tickspeed (zero, negative, extremes), and unk8 set
//! so the step ends below, exactly at (the equal compare), or one past unk9
//! (the clamp boundary). unk9 is 0 (early return), 1, 2, 128, 255 or any
//! byte, so the sign bit is both set and clear. unk4 stays in [-2048, 2047]
//! so temp_s1 * unk8 cannot overflow the C's int; the scratchpad word the
//! default arm reads is set to a small range for the same reason. rsin is a
//! recording stub returning -4096..4096, the range of a real sine.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BE01C));

pub const classes = [_][:0]const u8{ "idle", "clamp", "reach", "run" };

pub const stubs = struct {
    pub fn rsin(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return @as(c_int, ctx.value(u16) % 8193) - 4096;
    }
};

/// The step the C adds to unk8 this run: vs_gametime_tickspeed >> 1, as a byte.
fn step() u8 {
    const t: i32 = v.g.vs_gametime_tickspeed().*;
    return @truncate(@as(u32, @bitCast(t >> 1)));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = ctx.alloc(v.func_800BDF6C_t);

    v.g.vs_gametime_tickspeed().* = ctx.pick(i32, &.{ 0, 1, 2, 3, -1, -2, 0x7FFF_FFFF, -0x8000_0000, 0x400 });

    a.unk0.u8[1] = ctx.edge(u8, &.{ 0, 1, 2, 3, 4, 0x80, 0xFF });
    a.unk4 = if (ctx.chance(2))
        ctx.edge(i16, &.{ 0, 1, -1, 127, 2047, -2048 })
    else
        ctx.range(i16, -2048, 2047);
    a.unk6 = ctx.pick(i16, &.{ 0, 1, -1, 4096, 32767, -32768 });
    a.unk9 = ctx.pick(u8, &.{ 0, 1, 2, 128, 255 });
    a.unk8 = ctx.int(u8);

    const inc = step();
    // The step ends exactly on unk9: the `unk9 < unk8` compare sees equal sides.
    if (ctx.chance(3)) a.unk8 = a.unk9 -% inc;
    // The step ends one past unk9: the clamp boundary.
    if (ctx.chance(6)) a.unk8 = a.unk9 -% inc +% 1;

    // The scratchpad word the default arm reads (0x1F800088).
    const s: *i32 = @ptrFromInt(zd.scratchpad + 0x88);
    s.* = ctx.range(i32, -4096, 4096);

    return .{a};
}

pub fn classify(args: Args) usize {
    const a = args[0].?;
    if (a.unk9 == 0) return 0;
    const sum: u8 = a.unk8 +% step();
    if (sum > a.unk9) return 1;
    if (sum == a.unk9) return 2;
    return 3;
}
