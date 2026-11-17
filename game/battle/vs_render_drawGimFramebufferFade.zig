//! Draws the two frame sprites of a gim image (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
const sprite_item = @import("common").sprite_item;

const Count = *const fn () callconv(.c) c_int;
const Anchor = *const fn (c_int, c_int, c_int) callconv(.c) void;

/// orig: vs_render_drawGimFramebufferFade (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Draws the image's two frame sprites, offset by the frame buffer's x position.
pub fn vs_render_drawGimFramebufferFade(gim: ?*v.gim_t, arg1: c_int, prims: [*c]c_uint) callconv(.c) void {
    const temp_s0: c_int = @as(c_int, gim.?.unk3);

    if (temp_s0 == 0x80) {
        c.vs_render_drawGimTiles(@ptrCast(gim), arg1, prims);
        return;
    }

    v.g.D_800F51C8().* = 1;

    c.vs_render_drawGimTiles(@ptrCast(gim), arg1, prims);

    var var_s1: c_int = 0;
    if (v.g.vs_main_frameBuf().* == 0) {
        var_s1 = 0x140;
    }

    const new_var: c_uint = psx.conv(c_uint, @as(c_int, 0x120));

    // ROOD_PC: the pair copies the shown frame buffer, which in widescreen is the whole view's picture: it covers the view (PC_ANCHOR_COVER),
    // placed here, not left to gpu_hw.c's screen-copy guess.
    const pair_start: c_int = if (psx.weak(Count, "pc_frame_count")) |count| count() else 0;
    // (no texel word: the packets' was whatever the buffer held; they copy the frame buffer in 15-bit colour)
    _ = sprite_item.sprite(prims, 0x80 -% temp_s0, 0, 0xF00100, sprite_item.pageMode((psx.conv(c_uint, var_s1) >> 6) | new_var), 0);
    _ = sprite_item.sprite(prims, 0x80 -% temp_s0, 0x100, 0xF00040, sprite_item.pageMode(psx.conv(c_uint, (var_s1 +% 0x100) >> 6) | new_var), 0);
    if (psx.weak(Anchor, "pc_frame_anchor")) |anchor| {
        anchor(pair_start, psx.weak(Count, "pc_frame_count").?(), 0x15);
    }
}
