//! zig_diff spec: pc_battle_camera_target -> scene_submit.battleCameraTarget (146C.c, scene_submit.inc).
//! Inputs: the character state present or not, its weapon drawn flag (0, 1, 2), the action block
//! present or not, the chosen candidate (0xFF, 0..22 and 23, 255 and others), the target-chosen
//! count unk2C00 (0 or not); each condition at its edge with the rest satisfied half the time.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").battleCameraTarget));

pub const classes = [_][:0]const u8{ "no-character", "weapon-away", "no-block", "no-candidate", "no-target", "candidate-out-of-range", "target" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const character = ctx.alloc(v.vs_battle_actor);
    v.g.vs_battle_characterState().* = if (ctx.chance(8)) null else character;
    character.weaponDrawn = ctx.edge(i32, &.{ 1, 1, 1, 1, 0, 2, -1 });
    const block = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = if (ctx.chance(8)) null else block;
    block.unk298D = ctx.edge(u8, &.{ 0, 1, 11, 22, 22, 23, 24, 0xFE, 0xFF });
    block.unk2C00 = ctx.pick(u16, &.{ 0, 1, 0x100, 0xFFFF });
    return .{ctx.alloc([3]c_int)};
}

pub fn classify(args: Args) usize {
    _ = args;
    const character = v.g.vs_battle_characterState().* orelse return 0;
    if (character.weaponDrawn != 1) return 1;
    const block = v.g.D_800F19CC().* orelse return 2;
    if (block.unk298D == 0xFF) return 3;
    if (block.unk2C00 == 0) return 4;
    if (block.unk298D >= 23) return 5;
    return 6;
}
