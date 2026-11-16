//! The five-panel screen wipe of the battle transition (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const gte = common.gte;
const v = @import("views_146C");
const c = @import("c_146C");
const sprite_item = common.sprite_item;

/// orig: vs_render_drawFramebufferEffect (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Draws the five wipe panels into the ordering table `ot` and steps the wipe state machine.
pub fn vs_render_drawFramebufferEffect(ot: ?*anyopaque) callconv(.c) void {
    var sp18: v.MATRIX = undefined;
    // translation.vz and the pad words are uninitialised in the original; zeroed here.
    var translation = std.mem.zeroes(v.VECTOR);
    // vertex.pad is never read by the GTE; zeroed like translation.
    var vertex = std.mem.zeroes(v.SVECTOR);
    var sp50: v.VECTOR = undefined;
    var sp60 = std.mem.zeroes(v.RECT);

    _ = c.memset(&sp60, @as(c_int, 0), @as(usize, 8));

    if (v.g.vs_render_framebufferEffectState().* == 2) {
        translation.vx = v.g.vs_render_framebufferEffectOffset().unk0 +% 0xA0;
        translation.vy = v.g.vs_render_framebufferEffectOffset().unk4 +% 0x70;
        _ = c.RotMatrix_gte(@ptrCast(v.g.vs_render_framebufferEffectRotation()), psx.cptr(c.MATRIX, &sp18));
        _ = c.TransMatrix(psx.cptr(c.MATRIX, &sp18), psx.cptr(c.VECTOR, &translation));
        _ = c.ScaleMatrix(psx.cptr(c.MATRIX, &sp18), @ptrCast(v.g.vs_render_framebufferEffectScale()));
        const view = gte.viewOf(&sp18, gte.no_screen);

        const m30 = v.g.vs_render_framebufferEffectScale();
        const code = v.g.vs_render_framebufferEffectTint();
        const rgb: c_int = @as(c_int, code.r0) | (@as(c_int, code.g0) << 8) | (@as(c_int, code.b0) << 16);
        const slot: ?*anyopaque = @ptrFromInt(@intFromPtr(ot) +% @as(usize, v.g.vs_render_framebufferEffectDepth().* *% 4));
        // The picture drawn so far (the frame buffer, 64 across for each of five strips; the other buffer when vs_render_framebufferEffectUsesPreviousFrame), semi-transparent.
        const buffer: c_int = if (v.g.vs_render_framebufferEffectUsesPreviousFrame().* != 0) v.g.vs_main_frameBuf().* ^ 1 else v.g.vs_main_frameBuf().* & 1;
        if ((@as(c_int, v.g.vs_render_framebufferEffectRotation()[2]) & 0xFFF) != 0 or m30[0] != 0x1000 or m30[1] != m30[0]) {
            // Rotated or scaled: five quads (this buffer unless the rotation's low bits are clear and vs_render_framebufferEffectUsesPreviousFrame is set).
            const other: c_int = if ((psx.conv(u16, v.g.vs_render_framebufferEffectRoll().*) & 0xFFF) == 0 and v.g.vs_render_framebufferEffectUsesPreviousFrame().* != 0) v.g.vs_main_frameBuf().* ^ 1 else v.g.vs_main_frameBuf().* & 1;
            var s1: c_int = 0;
            while (s1 < 5) : (s1 +%= 1) {
                const tpage: c_int = c.GetTPage(2, v.g.vs_render_framebufferEffectBlendMode().*, s1 *% 64 +% (other *% 320), 0);
                var xy: [4]c_int = undefined;
                const corners = [4][2]c_int{ .{ s1 *% 64 -% 160, -0x70 }, .{ s1 *% 64 -% 96, -0x70 }, .{ s1 *% 64 -% 160, 0x6F }, .{ s1 *% 64 -% 96, 0x6F } };
                for (corners, 0..) |corner, k| {
                    vertex.vx = psx.conv(i16, corner[0]);
                    vertex.vy = psx.conv(i16, corner[1]);
                    vertex.vz = 0;
                    gte.transformTo(&sp50, &view, &vertex);
                    xy[k] = (@as(c_int, psx.conv(i16, sp50.vx)) & 0xFFFF) | (@as(c_int, psx.conv(i16, sp50.vy)) << 16);
                }
                // Texels u 0..64 (the last strip 63), v 0..223.
                const right: c_int = if (s1 == 4) 63 else 64;
                const uv = [4]c_int{ 0, right, 223 << 8, right | (223 << 8) };
                _ = sprite_item.ft4(slot, false, true, tpage, 0, rgb, &xy, &uv);
            }
        } else {
            // Unrotated and unscaled: five sprites, each with its draw mode.
            var s1: c_int = 0;
            while (s1 < 5) : (s1 +%= 1) {
                const temp_s0: c_int = s1 << 6;
                const page: c_int = c.GetTPage(2, v.g.vs_render_framebufferEffectBlendMode().*, temp_s0 +% (buffer *% 0x140), 0);
                const x: c_int = @as(c_int, psx.conv(i16, @as(c_int, psx.conv(u16, v.g.vs_render_framebufferEffectOffset().unk0)) +% temp_s0));
                const item = sprite_item.sprite(slot, 0x100, (x & 0xFFFF) | (@as(c_int, psx.conv(i16, v.g.vs_render_framebufferEffectOffset().unk4)) << 16), 64 | (223 << 16), 0, 0);
                sprite_item.color(item, rgb, true);
                sprite_item.drawModeWindow(slot, 0x200 | (page & 0x1FF), 0);
            }
        }
    }

    if (v.g.vs_render_framebufferEffectState().* == 1) {
        v.g.vs_render_framebufferEffectState().* = 2;
        return;
    }
    if (v.g.vs_render_framebufferEffectState().* >= 3) {
        v.g.vs_render_framebufferEffectState().* +%= 1;
        if (v.g.vs_render_framebufferEffectState().* >= 5) {
            v.g.vs_render_framebufferEffectState().* = 0;
        }
    }
}
