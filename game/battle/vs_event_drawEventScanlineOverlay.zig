//! Draws the striped scanline primitives for the battle screen and sets the draw tpage
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const sprite_item = common.sprite_item;

/// orig: vs_event_drawEventScanlineOverlay (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Advances the stripe phase, adds a flat line primitive per shaded row to the ordering table, then sets the draw tpage.
pub fn vs_event_drawEventScanlineOverlay() callconv(.c) void {
    const ot = common.scratch.get().unk8;

    v.g.vs_event_scanlineOverlayFrame().* +%= 1;

    var i: i16 = 0;
    while (@as(c_int, i) < @as(c_int, v.g.vs_render_viewportHeight().*)) : (i +%= 1) {
        const temp: c_uint = psx.conv(c_uint, ((@as(c_int, i) +% (@as(c_int, v.g.vs_event_scanlineOverlayFrame().*) >> 1)) & 1) << 5);
        if (temp != 0) {
            // A semi-transparent grey line across row i.
            const shade: c_int = @bitCast(temp & 0xFF);
            sprite_item.lineFlat(ot, true, shade | (shade << 8) | (shade << 16), @as(c_int, i) << 16, (@as(c_int, i) << 16) | @as(c_int, @as(u16, @bitCast(v.g.vs_render_viewportWidth().*))));
        }
    }

    // The draw mode the lines drew in: additive-subtractive blend (abr 2), dithering and drawing on the display on.
    sprite_item.drawMode(ot, 0x640);
}
