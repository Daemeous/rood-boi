//! zig_diff spec: pc_charge_skip_battle_state -> charge_skip.battleState (146C.c, charge_skip.inc).
//! Inputs: the action block present or not, BATTLE's mode 2 or another, the camera mode, the
//! current slot's caster and action.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").battleState));

pub const classes = [_][:0]const u8{ "no-block", "other-mode", "action-mode" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F19CC().* = if (ctx.chance(4)) null else ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F196C().* = ctx.edge(u32, &.{ 2, 2, 1, 12, 0, 0x8000_0002 });
    v.g._cameraMode().* = ctx.pick(u32, &.{ 0, 4, 7, 0x8000_0000 });
    if (v.g.D_800F19CC().*) |block| block.unk0 = ctx.int(u32);
    return .{ ctx.alloc(c_int), ctx.alloc(c_int), ctx.alloc(c_int) };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800F19CC().* == null) return 0;
    return if (v.g.D_800F196C().* == 2) 2 else 1;
}
