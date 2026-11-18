//! The target marker drawn at a 3D position (pc/src/game/BATTLE/BATTLE.PRG/58578.c).
const common = @import("common");
const psx = common.psx;
const gte = common.gte;
const v = @import("views_58578");
const c = @import("c_58578");
const sprite_item = common.sprite_item;

/// orig: func_800C28AC (pc/src/game/BATTLE/BATTLE.PRG/58578.c)
/// Projects `position` to the screen and links the target marker's sprites into the ordering table; `arg1` selects the marker half (0 links the frame primitives too).
pub fn func_800C28AC(position: ?*v.SVECTOR, arg1: c_int) callconv(.c) void {
    // psx_get yields a void*: the C steps back 4 bytes, not 4 words.
    const ot = psx.byteAt(c_uint, psx.get(v.g.vs_scratch().unk4).?, -4);

    var pcMarker: v.pc_classic_target_marker = undefined;

    const frame = gte.frame();
    // PC_GTE_UNNARROWED: widescreen keeps the marker 2D.
    v.g.pc_gte_anchored().* +%= 1;
    const projected = gte.project(&frame, position.?);
    v.g.pc_gte_anchored().* -%= 1;

    const xy: c_int = projected.sxy;
    const xyu: c_uint = @bitCast(xy);

    const step = (@as(c_int, v.g.vs_battle_positionCursorBlinkFrame().*) +% 1) & 0xF;
    v.g.vs_battle_positionCursorBlinkFrame().* = psx.conv(u8, step);

    pcMarker.x = @as(c_int, psx.conv(i16, xy));
    pcMarker.y = xy >> 16;
    pcMarker.lock = arg1;
    pcMarker.step = @as(c_int, v.g.vs_battle_positionCursorBlinkFrame().*);
    const pcMarkerFirst = v.g.vs_scratch().unk0;

    const otPtr: [*c]c_uint = @ptrFromInt(@intFromPtr(ot));

    if (arg1 == 0) {
        // The marker's frame: two textured quads (page 12, the clut 0x37FD), draw items placed at the slot where their packets linked.
        const brightness: c_int = 0x808080;
        const quad1_xy = [4]c_int{ @bitCast(xyu +% 0xFFF7FFF3), @bitCast(xyu +% 0xFFF7FFFD), @bitCast(xyu +% 0xFFFEFFF3), @bitCast(xyu +% 0xFFFEFFFD) };
        const quad1_uv = [4]c_int{ 0x38F1, 0x38E7, 0x3FF1, 0x3FE7 };
        _ = sprite_item.ft4(otPtr, false, false, 0x0C, 0x37FD, brightness, &quad1_xy, &quad1_uv);
        const quad2_xy = [4]c_int{ @bitCast(xyu +% 0x1FFF3), @bitCast(xyu +% 0x1FFFD), @bitCast(xyu +% 0x8FFF3), @bitCast(xyu +% 0x8FFFD) };
        const quad2_uv = [4]c_int{ 0x40F1, 0x40E7, 0x47F1, 0x47E7 };
        _ = sprite_item.ft4(otPtr, false, false, 0x0C, 0x37FD, brightness, &quad2_xy, &quad2_uv);
    }

    var i: c_int = 0;
    while (i < 3) : (i +%= 1) {
        const index: c_int = i +% arg1 *% 2;
        const brightness: c_int = if (index == 2)
            @as(c_int, v.g.vs_battle_cursorBrightnessAnimation()[v.g.vs_battle_positionCursorBlinkFrame().*])
        else
            0x80;
        _ = sprite_item.sprite(otPtr, brightness, xy +% psx.at(v.g.vs_battle_positionCursorSpriteOffsets(), index).*, psx.at(v.g.vs_battle_positionCursorSpriteSizes(), index).*, sprite_item.page, @bitCast(psx.conv(c_uint, psx.at(v.g.vs_battle_positionCursorSpriteTexCoords(), index).*)));
    }

    // ROOD_PC: PC_CLASSIC target marker site (classic_hook.h); weak, tested for presence.
    const Site = *const fn (c_int, ?*anyopaque, c_int, c_int, ?*anyopaque, ?*anyopaque) callconv(.c) void;
    if (psx.weak(Site, "pc_classic_site")) |site| {
        site(
            22,
            &pcMarker,
            pcMarker.x,
            pcMarker.y,
            pcMarkerFirst,
            v.g.vs_scratch().unk0,
        );
    }
}
