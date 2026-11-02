//! zig_diff spec: func_800B7490 -> func_800B7490.func_800B7490 (4A0A8.c).
//! Inputs: a 16-byte block whose bytes 1-2 (the actor id) and 3-4 (the flag)
//! vs_battle_getShort reads back, so the actor covers bit 12 set and clear,
//! the sign bit, and the low five id bits zero or not. func_8007CF64 returns
//! NULL or a fresh object; func_8007CF18 returns 1, a value that is 1 only
//! after the u16 truncation (0x10001), a value that is not 1 (0x101 truncates
//! to 257), or any c_int. func_800A1108 writes the three position bytes the C
//! reads back. The state flags stay in 0..255 (the table's bounds are not
//! known to the spec). arg1 is unused by the C and is random.
const std = @import("std");
const zd = @import("zdiff");
const c = @import("c_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B7490));

pub const classes = [_][:0]const u8{ "bit12-clear", "bit12-clear-signbit", "bit12-set", "bit12-set-id0" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const bytes: [*]u8 = ctx.allocBytes(16, 16);
    const actor = ctx.pick(u16, &.{ 0x0000, 0x0001, 0x001F, 0x0020, 0x1000, 0x1001, 0x101F, 0x1020, 0x1FFF, 0x8000, 0x9000, 0xFFFF });
    const flag = ctx.edge(u16, &.{ 0, 1, 2, 7, 8, 100, 253 });
    std.mem.writeInt(u16, bytes[1..3], actor, .little);
    std.mem.writeInt(u16, bytes[3..5], flag, .little);
    const a1: c_short = @bitCast(ctx.int(u16));
    return .{ bytes, a1 };
}

pub fn classify(args: Args) usize {
    const b: [*]const u8 = args[0];
    const actor = std.mem.readInt(u16, b[1..3], .little);
    if ((actor & 0x1000) == 0) return if ((actor & 0x8000) != 0) 1 else 0;
    return if ((actor & 0x1F) == 0) 3 else 2;
}

pub const stubs = struct {
    // Reads the 16-bit value at the pointer, as the real callee does.
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = ctx;
        const bytes: [*]const u8 = @ptrCast(p);
        return @bitCast(std.mem.readInt(u16, bytes[0..2], .little));
    }

    // NULL (the C's test) or a valid object.
    pub fn func_8007CF64(ctx: *zd.Ctx, actorId: anytype) ?*c.vs_battle_actor {
        _ = actorId;
        if (ctx.value(u8) < 128) return null;
        return @ptrCast(@alignCast(ctx.allocBytes(64, 16)));
    }

    // Returns 1 (the C's test), values that only equal 1 after the C's
    // truncation to u16, and any other c_int.
    pub fn func_8007CF18(ctx: *zd.Ctx, actor: anytype) c_int {
        _ = actor;
        const r = ctx.value(u8);
        if (r < 96) return 1;
        if (r < 128) return 0x10001;
        if (r < 160) return 0x101;
        return ctx.value(c_int);
    }

    // Writes the three position bytes the C reads back.
    pub fn func_800A1108(ctx: *zd.Ctx, actor: anytype, out: anytype) c_int {
        _ = actor;
        const raw: ?*anyopaque = out;
        const p: [*]u8 = @ptrCast(raw.?);
        for (0..3) |i| p[i] = ctx.value(u8);
        return ctx.value(c_int);
    }
};
