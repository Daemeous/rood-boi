//! Per-frame update of the battle effect list: ages each node, fires its tick and end events, and expires it (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// The parent func_800D2ADC reads (the plugins hand it their own records in this layout): the node's position and
/// its flag bits, so the node's layout is not the callee's.
fn parentOf(n: *v.func_800D2904_t) v.func_800FB4C0_t {
    var parent = std.mem.zeroes(v.func_800FB4C0_t);
    parent.unkC.vx = n.unkC;
    parent.unkC.vy = n.unk10;
    parent.unkC.vz = n.unk14;
    psx.setBits(&parent, "unk60", psx.getBits(n, "unk60_0"));
    psx.setBits(&parent, "unk60_8", psx.getBits(n, "unk60_8"));
    return parent;
}

/// orig: vs_effects_tickEffectParticles (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Advances every effect node on the list: ages it, fires its tick event, counts its lifetime down and ends it.
pub fn vs_effects_tickEffectParticles(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const a = arg0.?;
    var next: ?*v.func_800D2904_t = psx.get(a.unk18);

    c.vs_effects_integrateParticleMotion(@ptrCast(next));
    // ROOD_PC: the head is read again after the call, as the C reads arg0->unk18 twice.
    c.vs_effects_drawAnimatedSpriteParticles(@ptrCast(psx.get(a.unk18)));

    var node: ?*v.func_800D2904_t = next;
    while (next != null) {
        const n = node.?;
        next = psx.get(n.next);
        n.unk74.fields.age +%= 1;
        if (n.tickEvent != 0) {
            var parent = parentOf(n);
            c.func_800D2ADC(@ptrCast(a), @as(c_int, n.tickEvent) -% 1, 0, 0, @ptrCast(&parent));
        }
        if (n.lifetime == -1) {
            if (n.sprite.unk6 == 0) {
                if (n.endEvent != 0) {
                    var parent = parentOf(n);
                    c.func_800D2ADC(@ptrCast(a), @as(c_int, n.endEvent) -% 1, 0, 0, @ptrCast(&parent));
                }
                c.vs_effects_releaseEffectParticle(@ptrCast(n), @ptrCast(a));
            }
        } else {
            n.lifetime -%= 1;
            if (n.lifetime == 0) {
                if (n.endEvent != 0) {
                    var parent = parentOf(n);
                    c.func_800D2ADC(@ptrCast(a), @as(c_int, n.endEvent) -% 1, 0, 0, @ptrCast(&parent));
                }
                c.vs_effects_releaseEffectParticle(@ptrCast(n), @ptrCast(a));
            }
        }
        node = next;
    }
    return 1;
}
