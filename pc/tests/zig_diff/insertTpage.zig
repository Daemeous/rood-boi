//! zig_diff spec: vs_battle_insertTpage -> insert_tpage.insertTpage (5BF94.c).
//! Inputs: the packet cursor (vs_scratch.unk0) a block in the PS1
//! scratchpad (its address has a high byte, 0x1F, which the link drops) or
//! the arena, `before` an arena word with random high bits (kept out of the
//! link), tpage any int.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").insertTpage));

pub const classes = [_][:0]const u8{ "arena", "scratchpad" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch = v.g.vs_scratch();
    const packet: *anyopaque = if (ctx.random.boolean()) @ptrFromInt(0x1F800100 + 8 * ctx.range(usize, 0, 15)) else @ptrCast(ctx.allocBytes(16, 16));
    psx.put(&scratch.unk0, packet);
    const before: *c_uint = @ptrCast(@alignCast(ctx.allocBytes(4, 16)));
    return .{ ctx.int(c_int), before };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (psx.rawOf(v.g.vs_scratch().unk0) >= 0x1F800000) 1 else 0;
}
