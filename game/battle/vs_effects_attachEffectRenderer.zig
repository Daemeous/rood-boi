//! Pushes a new effect node onto the head of the list at vs_effects_primaryInstances's unkD1C.unk34 and runs its renderer once (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_attachEffectRenderer (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Allocates an effect node, links it at the head of the effect list, and calls its renderer with step 1.
pub fn vs_effects_attachEffectRenderer(arg0: ?*v.D_800F53B8_t, renderer: c.effectExec, arg2: c_int) callconv(.c) ?*v.func_800D4910_t {
    const node: *v.func_800D4910_t = @as(*v.func_800D4910_t, @ptrCast(@alignCast(c.vs_main_allocHeapR(@as(c_uint, @sizeOf(v.func_800D4910_t))).?)));
    const base = arg0.?;
    node.next = @alignCast(psx.get(base.context.unk34));
    psx.put(&base.context.unk34, node);
    node.renderer = @ptrCast(renderer);
    node.unk8 = null;
    const call: c.effectExec = @ptrCast(node.renderer);
    _ = call.?(psx.cptr(c.func_800D4910_t, node), 1, arg2);
    return node;
}
