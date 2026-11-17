//! Unlinks a node from the vs_effects_secondaryInstances list and frees it (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_destroySecondaryEffectInstance (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Unlinks `entry` from the list at vs_effects_secondaryInstances, frees what it owns and the node.
pub fn vs_effects_destroySecondaryEffectInstance(entry: ?*v.vs_effects_EffectInstance) callconv(.c) void {
    var cur: ?*v.vs_effects_EffectInstance = v.g.vs_effects_secondaryInstances().*;
    if (cur == entry) {
        v.g.vs_effects_secondaryInstances().* = psx.get(entry.?.next);
    } else {
        while (psx.get(cur.?.next) != null) {
            if (psx.get(cur.?.next) == entry) {
                // psx_copy: the raw guest address of entry's successor.
                cur.?.next = entry.?.next;
                break;
            }
            cur = psx.get(cur.?.next);
        }
    }
    c.vs_effects_releaseAllEffectParticles(@ptrCast(entry));
    c.vs_effects_releaseEffectToken(@as(c_int, psx.getBits(entry.?, "unk14_0")));
    c.vs_main_freeHeapR(entry);
}
