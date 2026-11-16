//! Fills gim's sine table and steps its keystream-driven angle (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_render_activateGimLayer (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Marks gim `arg0` as set up; in mode 2 fills its sine table, in mode 3 calls vs_render_reserveTexturePages on the base gim.
pub fn vs_render_activateGimLayer(arg0: c_uint, _: ?*anyopaque, _: [*c]u8) callconv(.c) void {
    const base: *v.gim_t = v.g.vs_render_gimLayers().*.?;
    const gim: *v.gim_t = psx.at(base, arg0);

    psx.setBits(gim, "unk0_0", 1);

    if (psx.getBits(gim, "unk0_3") == 2) {
        const p = &gim.unk18;
        var angle: c_int = psx.conv(c_int, c.vs_battle_keystreamBits(9));
        var target: c_int = psx.conv(c_int, c.vs_battle_keystreamBits(10));

        var i: c_int = 0;
        while (i < 240) : (i +%= 1) {
            p[@as(usize, @intCast(i))] = psx.conv(i16, (c.rsin(angle) >> 3) +% 320);
            const step: c_int = psx.conv(c_int, c.vs_battle_keystreamBits(8) -% 64);
            const next: c_int = psx.conv(c_int, c.vs_battle_keystreamBits(10));

            if (target < angle) {
                angle -%= step;

                if (target >= angle) {
                    target = next;
                }
            } else {
                angle +%= step;

                if (angle >= target) {
                    target = next;
                }
            }

            if (angle < 0) {
                angle = 128;
                target = psx.conv(c_int, c.vs_battle_keystreamBits(10));
            }
        }
    }

    if (psx.getBits(gim, "unk0_3") == 3) {
        const sum: c_int = @as(c_int, base.unk2) +% @as(c_int, psx.getBits(base, "unk0_9"));
        _ = c.vs_render_reserveTexturePages(psx.conv(c_uint, sum), 1, 6);
    }
}
