//! Walks the battle's linked list of entries, running each entry's state step and returning the OR of their flags (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_updateSecondaryEffects (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Steps every list entry by its state and returns the OR of their flag bits.
pub fn vs_effects_updateSecondaryEffects() callconv(.c) c_int {
    var s1: ?*v.vs_effects_EffectInstance = v.g.vs_effects_secondaryInstances().*;
    var s3: c_int = 0;

    while (s1 != null) {
        const node = s1.?;
        v.g.vs_effects_currentInstance().* = node;
        const next = psx.get(node.next);

        switch (@as(c_int, node.context.unk3A)) {
            2 => {},
            3 => {
                c.vs_effects_acquireParticlePool(0xFF);
                c.vs_effects_setSecondaryEffectPhase(4, @ptrCast(node));
            },
            4 => {
                if (psx.get(v.g.vs_effects_package().*.?.curveBlock) != null) {
                    c.vs_effects_sampleAllEffectCurves(@as(c_int, psx.getBits(node, "unk14_16")));
                }

                c.vs_effects_updateParticleGravityAndDrag();

                var s0: ?*align(4) v.func_800D4910_t = psx.get(node.context.unk34);
                while (s0 != null) {
                    const entry = s0.?;
                    const next_entry = entry.next;
                    const Renderer = *const fn (*align(4) v.func_800D4910_t, c_int, c_int) callconv(.c) c_int;
                    const render: Renderer = @ptrCast(entry.renderer.?);
                    if (render(entry, 2, 0) == 0) {
                        // Entries are 16-byte heap blocks, so the alignment the C view asks of the pointer holds.
                        c.vs_effects_detachEffectRenderer(@ptrCast(node), @ptrCast(@alignCast(entry)));
                    }
                    s0 = next_entry;
                }

                if (c.vs_effects_runScript(@ptrCast(node)) == 0) {
                    c.vs_effects_destroySecondaryEffectInstance(@ptrCast(node));
                    c.vs_effects_releaseParticlePool();
                } else {
                    psx.setBits(node, "unk14_16", psx.conv(u16, @as(c_int, psx.getBits(node, "unk14_16")) +% 1));
                }
            },
            else => {},
        }

        s3 |= @as(c_int, psx.getBits(node, "unk14_8"));
        s1 = next;
    }
    return s3;
}
