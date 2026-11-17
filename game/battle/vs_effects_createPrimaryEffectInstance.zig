//! Inserts a new node into the sorted list headed by vs_effects_primaryInstances, keyed by unk9
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_createPrimaryEffectInstance (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Allocates a list node, fills it from `arg0` and links it into the list in key order; returns the node.
pub fn vs_effects_createPrimaryEffectInstance(arg0: ?*v.vs_effects_EffectSpawnRequest) callconv(.c) ?*v.vs_effects_EffectInstance {
    const newNode: *v.vs_effects_EffectInstance = @ptrCast(@alignCast(c.vs_main_allocHeapR(@sizeOf(v.vs_effects_EffectInstance)).?));

    c.vs_effects_initEffectInstance(@ptrCast(arg0), @ptrCast(newNode));

    var currentNode: ?*v.vs_effects_EffectInstance = v.g.vs_effects_primaryInstances().*;

    if (currentNode == null or arg0.?.priority < @as(i32, currentNode.?.unk9)) {
        psx.put(&newNode.next, currentNode);
        v.g.vs_effects_primaryInstances().* = newNode;
    } else {
        while (true) {
            // psx_copy/psx_set of guest pointer words: the following node is read once, its value is the same.
            const following = psx.get(currentNode.?.next);
            if (following == null or arg0.?.priority < @as(i32, following.?.unk9)) {
                psx.put(&newNode.next, following);
                psx.put(&currentNode.?.next, newNode);
                break;
            }
            currentNode = following;
        }
    }
    return newNode;
}
