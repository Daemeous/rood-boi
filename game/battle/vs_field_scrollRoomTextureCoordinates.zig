//! Scrolls a textured quad's UV window across the room geometry section (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const mpd_polygon = @import("mpd_polygon.zig");

/// orig: vs_field_scrollRoomTextureCoordinates (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Advances a scrolling UV window every `delay` ticks and rewrites the quad's texture coordinates in the room geometry.
pub fn vs_field_scrollRoomTextureCoordinates(arg0: ?*v.vs_field_TextureScrollEffect) callconv(.c) c_int {
    const quad = arg0.?;
    const base: usize = @intFromPtr(psx.get(v.g.vs_battle_roomData().geometrySection));

    // ++tick is a u8 increment: the stored (wrapped) value is what the comparison sees.
    quad.tick +%= 1;
    if (@as(c_int, quad.tick) > @as(c_int, quad.delay)) {
        const baseU: c_int = quad.baseU;
        const baseV: c_int = quad.baseV;
        const width: c_int = quad.width;
        const height: c_int = quad.height;
        const windowWidth: c_int = quad.windowWidth;
        const windowHeight: c_int = quad.windowHeight;

        var uPos: c_int = @as(c_int, quad.currentU) +% @as(c_int, quad.stepU);
        var vPos: c_int = @as(c_int, quad.currentV) +% @as(c_int, quad.stepV);
        quad.tick = 0;

        if (uPos < baseU) {
            uPos = baseU +% width -% windowWidth;
        }
        if (vPos < baseV) {
            vPos = baseV +% height -% windowHeight;
        }
        if (baseU +% width < uPos +% windowWidth) {
            uPos = baseU;
        }
        if (baseV +% height < vPos +% windowHeight) {
            vPos = baseV;
        }

        quad.currentU = psx.conv(u8, uPos);
        quad.currentV = psx.conv(u8, vPos);

        // base + geometryOffset: a signed offset.
        mpd_polygon.setUv(mpd_polygon.at(base, quad.geometryOffset), quad.u, quad.v, uPos, vPos);
    }
    return 1;
}
