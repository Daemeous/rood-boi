//! Inserts a new node into the sorted list headed by vs_effects_secondaryInstances (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_createSecondaryEffectInstance (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Allocates a node, fills it from `arg0` and links it into the list in order of `arg0.unk10`.
pub fn vs_effects_createSecondaryEffectInstance(arg0: ?*v.vs_effects_EffectSpawnRequest) callconv(.c) ?*v.vs_effects_EffectInstance {
    const raw = c.vs_main_allocHeapR(@sizeOf(v.vs_effects_EffectInstance)).?;
    const temp: *v.vs_effects_EffectInstance = @ptrCast(@alignCast(raw));
    c.vs_effects_initEffectInstance(@ptrCast(arg0), @ptrCast(temp));

    const a1: ?*v.vs_effects_EffectInstance = v.g.vs_effects_secondaryInstances().*;

    if (a1 == null or arg0.?.priority < @as(c_int, a1.?.unk9)) {
        psx.put(&temp.next, a1);
        v.g.vs_effects_secondaryInstances().* = temp;
    } else {
        var cur: *v.vs_effects_EffectInstance = a1.?;
        while (true) {
            const next_ = psx.get(cur.next);
            if (next_ == null or arg0.?.priority < @as(c_int, next_.?.unk9)) {
                psx.put(&temp.next, next_);
                psx.put(&cur.next, temp);
                break;
            }
            cur = next_.?;
        }
    }
    return temp;
}
