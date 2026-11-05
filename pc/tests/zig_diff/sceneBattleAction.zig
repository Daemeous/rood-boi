//! zig_diff spec: pc_battle_action -> scene_submit.battleAction (146C.c, scene_submit.inc).
//! Inputs: BATTLE's mode 2 or not, the action block present or not, the current slot (unk0 & 3)
//! with an action index around 256 (below: the actions table's type and unk2_0, set for the
//! index), the caster, the chain step, the aim point, the target count unk4A and the first
//! target's unk40 and actor, the camera mode.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").battleAction));

pub const classes = [_][:0]const u8{ "no-action", "plain-action", "effect-action", "high-index", "target", "no-target" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F196C().* = ctx.edge(u32, &.{ 2, 2, 2, 2, 1, 3 });
    const block = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = if (ctx.chance(8)) null else block;
    v.g._cameraMode().* = ctx.pick(u32, &.{ 0, 5, 7, 8, 9 });
    block.unk0 = ctx.int(u32);
    const slot = &block.unk854[block.unk0 & 3];
    slot.actionIndex = ctx.pick(u16, &.{ 0, 1, 0x8D, 254, 255, 256, 257, 0xFFFF });
    if (slot.actionIndex < 256) {
        const action = &v.g.actions()[slot.actionIndex];
        psx.setBits(action, "type", ctx.int(u2));
        psx.setBits(action, "unk2_0", ctx.int(u1));
    }
    slot.unk4.unk0.targetActor = ctx.pick(u8, &.{ 0, 1, 16, 255 });
    slot.unk4A = ctx.pick(u16, &.{ 0, 1, 2, 30, 0xFFFF });
    slot.unk4C[0].unk40 = ctx.edge(@TypeOf(slot.unk4C[0].unk40), &.{ 0, 0, 1, -1 });
    slot.unk4C[0].unk0.targetActor = ctx.int(u8);
    return .{ ctx.alloc(c_int), ctx.alloc(c_int), ctx.alloc(c_int), ctx.alloc(c_int), ctx.alloc(c_int), ctx.alloc(c_int), ctx.alloc([3]c_int), ctx.alloc(c_int) };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800F196C().* != 2) return 0;
    const block = v.g.D_800F19CC().* orelse return 0;
    const slot = &block.unk854[block.unk0 & 3];
    if (slot.actionIndex >= 256) return 3;
    if (psx.getBits(&v.g.actions()[slot.actionIndex], "unk2_0") == 0) return 1;
    if (slot.unk4A != 0 and slot.unk4C[0].unk40 == 0) return 4;
    return if (slot.unk4A == 0) 5 else 2;
}
