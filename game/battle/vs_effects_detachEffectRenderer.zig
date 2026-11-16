//! Unlinks a node from the list headed at owner.unkD1C.unk34 and frees it
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_detachEffectRenderer (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Frees the node's unk8 block, unlinks the node from the list and frees the node.
pub fn vs_effects_detachEffectRenderer(arg0: ?*v.D_800F53B8_t, arg1: ?*v.func_800D4910_t) callconv(.c) void {
    const owner = arg0.?;
    const node = arg1.?;

    if (node.unk8 != null) {
        c.vs_main_freeHeapR(node.unk8);
    }

    if (psx.get(owner.unkD1C.unk34) == arg1) {
        psx.put(&owner.unkD1C.unk34, node.next);
    } else {
        var prev: ?*align(4) v.func_800D4910_t = psx.get(owner.unkD1C.unk34);
        while (prev.?.next != arg1) {
            prev = prev.?.next;
        }
        prev.?.next = node.next;
    }

    c.vs_main_freeHeapR(arg1);
}
