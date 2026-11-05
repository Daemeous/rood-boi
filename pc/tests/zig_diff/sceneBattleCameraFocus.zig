//! zig_diff spec: pc_battle_camera_focus -> scene_submit.battleCameraFocus (146C.c, scene_submit.inc).
//! Inputs: the action block present or not, BATTLE's state D_800F196C (2 and the edges around it, and
//! values above 8 bits), the camera mode _cameraMode (0..4 and above 8 bits), the chosen candidate
//! unk298D (0..22, the range edges 23, 24 and 0xFF), the target-chosen count unk2C00 (0, 1, a value whose
//! low byte is 0, 0xFFFF), the candidate and area points at any value, and the camera's look-at (random
//! scratchpad words: negative values show the arithmetic shift). The output block starts random.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").battleCameraFocus));

pub const classes = [_][:0]const u8{ "no-block", "other-state", "candidate", "no-candidate", "area-point", "other-mode" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = if (ctx.chance(6)) null else block;
    v.g.D_800F196C().* = ctx.edge(u32, &.{ 2, 2, 2, 2, 2, 1, 3, 0, 0x102, 0x8000_0002, 0xFFFF_FFFF });
    v.g._cameraMode().* = ctx.edge(u32, &.{ 1, 1, 3, 3, 2, 2, 0, 4, 5, 0x101, 0x102, 0xFFFF_FFFF });
    block.unk298D = ctx.edge(u8, &.{ 0, 1, 11, 21, 22, 22, 23, 24, 0xFE, 0xFF });
    block.unk2C00 = ctx.pick(u16, &.{ 1, 1, 0, 0x100, 0xFFFF });
    return .{ctx.alloc([14]c_int)};
}

pub fn classify(args: Args) usize {
    _ = args;
    const block = v.g.D_800F19CC().* orelse return 0;
    if (v.g.D_800F196C().* != 2) return 1;
    const mode = v.g._cameraMode().*;
    if (mode == 1 or mode == 3) {
        return if (block.unk298D != 0xFF and block.unk2C00 != 0 and block.unk298D < 23) 2 else 3;
    }
    return if (mode == 2) 4 else 5;
}
