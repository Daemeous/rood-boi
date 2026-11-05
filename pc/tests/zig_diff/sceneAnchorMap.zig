//! zig_diff spec: anchor_map -> scene_submit.anchorMap (146C.c, scene_submit.inc).
//! Inputs: the map's first packet (null or a block) and the packet cursor (vs_scratch.unk0:
//! null, a block or any address). pc_hw_anchor_packets is the always-linked weak hook, a
//! recording stub: the call, its arguments and the constant 2 are compared.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").anchorMap));

pub const classes = [_][:0]const u8{ "null-start", "block-start" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
    scratch.unk0.raw = if (ctx.chance(4)) 0 else psx.addr(ctx.allocBytes(64, 16));
    scratch.unk4.raw = psx.addr(ctx.allocBytes(64, 16));
    return .{if (ctx.chance(4)) null else ctx.allocBytes(64, 16)};
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
