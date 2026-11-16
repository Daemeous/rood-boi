//! Builds the battle node chain: `count` linked nodes in a doubly-linked list (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_acquireParticlePool (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Allocates `count` linked nodes into the global chain; later calls only count up.
pub fn vs_effects_acquireParticlePool(count: c_int) callconv(.c) void {
    if (v.g.vs_effects_particlePoolReferences().* != 0) {
        v.g.vs_effects_particlePoolReferences().* +%= 1;
        return;
    }

    // sizeof * count is computed in size_t and truncated to the u_int argument: the low 32 bits are the same on every target.
    const bytes: c_uint = @as(c_uint, @sizeOf(v.func_800D2904_t)) *% psx.conv(c_uint, count);
    const raw = c.vs_main_allocHeapR(bytes);
    v.g.vs_effects_particlePool().* = @ptrCast(@alignCast(raw));
    const nodes = v.g.vs_effects_particlePool().*.?;

    psx.put(&nodes.previous, null);
    psx.put(&nodes.next, psx.at(nodes, 1));

    var i: c_int = 1;
    while (i < count -% 1) : (i +%= 1) {
        psx.put(&psx.at(nodes, i).previous, psx.at(nodes, i -% 1));
        psx.put(&psx.at(nodes, i).next, psx.at(nodes, i +% 1));
    }

    v.g.vs_effects_usedParticleCount().* = 0;
    v.g.vs_effects_particleCapacity().* = count;
    v.g.vs_effects_particlePoolReferences().* = 1;
    psx.put(&psx.at(nodes, i).previous, psx.at(nodes, i -% 1));
    psx.put(&psx.at(nodes, i).next, null);
    v.g.vs_effects_freeParticles().* = v.g.vs_effects_particlePool().*;
}
