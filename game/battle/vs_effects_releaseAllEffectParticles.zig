//! Returns a chain of effect records to the free list and debits the free count
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_effects_releaseAllEffectParticles (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Splices the owner's record chain onto the head of the free list, then debits the free count by the owner's count and clears it.
pub fn vs_effects_releaseAllEffectParticles(arg0: ?*v.vs_effects_EffectInstance) callconv(.c) void {
    const owner = arg0.?;
    const temp_a1 = psx.get(owner.unk18);
    var var_a0 = temp_a1;

    if (temp_a1 == null) {
        return;
    }

    while (psx.get(var_a0.?.next)) |n| {
        var_a0 = n;
    }

    psx.put(&var_a0.?.next, v.g.vs_effects_freeParticles().*);
    v.g.vs_effects_freeParticles().* = temp_a1;
    v.g.vs_effects_usedParticleCount().* -%= @as(c_int, owner.unk8);
    owner.unk8 = 0;
}
