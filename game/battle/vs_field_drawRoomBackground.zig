//! The battle camera's scene draw: the background tile layout, the full-screen
//! flash quad, and the view matrices (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");
const sprite_item = common.sprite_item;

const AnchorPackets = *const fn (?*anyopaque, ?*anyopaque, c_int) callconv(.c) void;

/// orig: vs_field_drawRoomBackground (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Draws the scene: the background layout when one is loaded, else the flash quad when set, else the camera's view matrices.
pub fn vs_field_drawRoomBackground(arg0: ?*v.VECTOR) callconv(.c) void {
    const scratch = v.g.vs_scratch();

    if (v.g.vs_field_isTiledBackgroundEnabled().* != 0 and v.g.vs_field_loadedBackgroundLayout().* != null) {
        const layout = v.g.vs_field_loadedBackgroundLayout().*.?;
        if (psx.conv(c_uint, layout.width -% 1) < 31 and psx.conv(c_uint, layout.height -% 1) < 31) {
            const x: c_int = @divTrunc(v.g.vs_view().camera.angles.vy *% @as(c_int, v.g.vs_field_backgroundParallaxScaleXFixed12().*), 4096) -% @as(c_int, v.g.vs_field_backgroundOffsetX().*);
            const y: c_int = @as(c_int, v.g.vs_field_backgroundOffsetY().*) -% @divTrunc(v.g.vs_view().camera.angles.vx *% @as(c_int, v.g.vs_field_backgroundParallaxScaleYFixed12().*), 4096);

            // ROOD_PC: PC_ANCHOR_PACKETS(PC_ANCHOR_SCENE, ...) around the tile draw (pc/src/widescreen.h).
            const PC_ANCHOR_SCENE: c_int = 0x14;
            const anchor_start: ?*anyopaque = @ptrFromInt(@as(usize, psx.rawOf(scratch.unk0)));
            c.vs_field_drawRoomBackgroundTiles(psx.cptr(c.vs_battle_backgroundLayout, layout), x, y, @as(c_int, v.g.vs_field_legacyBackgroundTexturePage().*));
            if (psx.weak(AnchorPackets, "pc_hw_anchor_packets")) |hook| {
                hook(anchor_start, @ptrFromInt(@as(usize, psx.rawOf(scratch.unk0))), PC_ANCHOR_SCENE);
            }
        }
    } else if (v.g.vs_field_isSolidBackground().* != 0) {
        // The full-screen flash: one textured quad (a single texel of the page, stretched), modulated by the flash colour, as a draw
        // item at the far slot of the table.
        var rgb: c_int = undefined;
        const b = v.g.D_800F1BB0();
        if (b.unk6 != 0) {
            var red: c_int = 128 +% (@as(c_int, b.unk0) *% 4);
            var green: c_int = 128 +% (@as(c_int, b.unk1) *% 4);
            var blue: c_int = 128 +% (@as(c_int, b.unk2) *% 4);
            red = @as(c_int, psx.conv(i16, c.vs_battle_clamp(psx.conv(i16, red), 0, 255)));
            green = @as(c_int, psx.conv(i16, c.vs_battle_clamp(psx.conv(i16, green), 0, 255)));
            blue = @as(c_int, psx.conv(i16, c.vs_battle_clamp(psx.conv(i16, blue), 0, 255)));
            rgb = (red & 0xFF) | ((green & 0xFF) << 8) | ((blue & 0xFF) << 16);
        } else {
            rgb = 0x808080;
        }
        const xy = [4]c_int{ 0, 320, 240 << 16, 320 | (240 << 16) };
        const uv = [4]c_int{ 248 | (248 << 8), 249 | (248 << 8), 248 | (249 << 8), 249 | (249 << 8) };
        const ot: ?*anyopaque = @ptrFromInt(@as(usize, psx.rawOf(scratch.unk4) +% 0x1FFC));
        _ = sprite_item.ft4(ot, false, false, 0x11, 0x38B1, rgb, &xy, &uv);
    } else {
        var angles: *v.VECTOR = undefined;
        if (arg0) |given| {
            angles = given;
        } else if (psx.get(v.g.vs_battle_roomData().sectionB)) |room| {
            angles = @ptrCast(&room.unk14);
        } else {
            angles = @ptrCast(v.g.D_800F1CE0());
        }

        var position: v.VECTOR = undefined;
        position.vx = @divTrunc(v.g.vs_view().camera.position.vx, 4096);
        position.vy = @divTrunc(v.g.vs_view().camera.position.vy, 4096);
        position.vz = @divTrunc(v.g.vs_view().camera.position.vz, 4096);
        position.vx *%= v.g.vs_view().camera.farClip;
        position.vy *%= v.g.vs_view().camera.farClip;
        position.vz *%= v.g.vs_view().camera.farClip;
        position.vx = @divTrunc(position.vx, 4096);
        position.vy = @divTrunc(position.vy, 4096);
        position.vz = @divTrunc(position.vz, 4096);

        common.gte.setFrame(&v.g.vs_view().viewMatrix); // vs_render_buildScaleCompensatedRoomView and vs_render_buildRoomGeometryViewMatrix take the frame's translation

        var matrix: v.MATRIX = undefined;
        if (v.g.vs_view().camera.farClip != 4096) {
            c.vs_render_buildScaleCompensatedRoomView(psx.cptr(c.VECTOR, &position), psx.cptr(c.VECTOR, angles), psx.cptr(c.MATRIX, &matrix));
        } else {
            c.vs_render_buildRoomGeometryViewMatrix(psx.cptr(c.VECTOR, &position), psx.cptr(c.VECTOR, angles), psx.cptr(c.MATRIX, &matrix));
        }
        const background = common.gte.viewOf(&matrix, common.gte.no_screen);
        c.vs_render_drawSkyDome(@ptrCast(v.g.vs_field_legacyBackgroundGradientPackets()), @ptrCast(&background));
    }
}
