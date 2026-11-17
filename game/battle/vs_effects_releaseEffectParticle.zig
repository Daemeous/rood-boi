//! Unlinks an entry from its doubly linked list and pushes it onto the free list
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_effects_releaseEffectParticle (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Unlinks `arg0` from its list, pushes it onto the free list and updates both counts.
pub fn vs_effects_releaseEffectParticle(arg0: ?*v.func_800D2904_t, arg1: ?*v.vs_effects_EffectInstance) callconv(.c) void {
    const self = arg0.?;
    const owner = arg1.?;

    if (psx.get(self.next)) |next| {
        next.previous = self.previous;
    }

    if (psx.get(self.previous)) |prev| {
        prev.next = self.next;
    } else {
        owner.unk18 = psx.get(self.next);
    }

    psx.put(&self.next, v.g.vs_effects_freeParticles().*);
    v.g.vs_effects_freeParticles().* = self;
    v.g.vs_effects_usedParticleCount().* -%= 1;
    owner.unk8 -%= 1;
}
