//! zig_diff spec: func_800B6580 -> pc.turn_request.requestTurn (D2: pc/src/battle_motion.c).
//! Inputs: an actor slot (0-16) a random-filled block (0x1900 bytes) whose
//! byte 0xB has the low nibble clear (the turn starts) or set (it returns at
//! once), and whose link byte 0x17FD is 0, 1 (no recursion) or 2-16 (the
//! linked actor gets the same request). Links only point at higher slots, so
//! the recursion ends; every slot is its own block, and the position is two
//! arena ints. b is an edge or any int, so `b << 16` loses its top bits. The
//! C calls only itself, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_motion");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").requestTurn));

pub const classes = [_][:0]const u8{ "pending", "starts-no-link", "starts-linked" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    for (0..17) |i| {
        const model = ctx.allocBytes(0x1900, 16);
        for (model[0..0x1900]) |*byte| byte.* = ctx.int(u8);
        // Byte 0xB: the low nibble clear most of the time, so the turn starts.
        if (!ctx.chance(4)) model[0xb] &= 0xf0;
        if (ctx.chance(5)) model[0xb] = ctx.edge(u8, &.{ 0x0F, 0x01, 0x10, 0xFF });
        // Links: 0/1 (none), or a higher slot 2..16 (strictly above this one).
        model[0x17fd] = switch (ctx.range(u8, 0, 3)) {
            0 => 0,
            1 => 1,
            else => if (i + 1 <= 16) ctx.range(u8, @intCast(@max(i + 1, 2)), 16) else 0,
        };
        table[i] = @ptrCast(model);
    }
    const id = ctx.range(c_int, 0, 16);
    const position: [*]c_int = @ptrCast(@alignCast(ctx.allocBytes(8, 16)));
    position[0] = ctx.int(c_int);
    position[1] = ctx.int(c_int);
    const b = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000, 0xFFFF, 0x10000, 0x12345 });
    return .{ id, position, b };
}

pub fn classify(args: Args) usize {
    const model: [*]const u8 = @ptrCast(v.g.D_800F4538()[@intCast(args[0])].?);
    if (model[0xb] & 0xf != 0) return 0;
    return if (model[0x17fd] >= 2) 2 else 1;
}
