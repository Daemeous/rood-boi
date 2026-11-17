//! Takes a particle node from the free list (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_allocateEffectParticle (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Pops vs_effects_freeParticles (func_800CE644 first when it is empty: the C then reads through NULL), clears it and pushes it on the script's list (unk18, count unk8).
pub fn vs_effects_allocateEffectParticle(script: ?*v.vs_effects_EffectInstance) callconv(.c) ?*v.func_800D2904_t {
    const s = script.?;
    const free = v.g.vs_effects_freeParticles().*;
    if (free == null) {
        _ = c.func_800CE644(0x14);
    }
    const node = free.?; // the C goes on with NULL (a crash)
    c.vs_effects_clearTextureAnimation(@ptrCast(&node.sprite));
    node.unk74.fields.age = 0xFF;
    psx.put(&node.previous, null);
    v.g.vs_effects_usedParticleCount().* +%= 1;
    v.g.vs_effects_freeParticles().* = psx.get(node.next);
    psx.put(&node.next, psx.get(s.unk18));
    if (psx.get(s.unk18)) |head| {
        psx.put(&head.previous, node);
    }
    psx.put(&s.unk18, node);
    s.unk8 +%= 1;
    return node;
}
