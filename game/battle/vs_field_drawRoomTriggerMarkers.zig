//! Ticks the room's section-9 timers and draws the ones still running (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_drawRoomTriggerMarkers (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Advances each room section-9 timer and shows its effect while the timer runs.
pub fn vs_field_drawRoomTriggerMarkers() callconv(.c) void {
    const marker: *v.vs_field_triggerMarkerBlock_t = @ptrCast(&v.g.vs_battle_mapCompletionFlags()[16]); // the marker's record (the tail of vs_battle_mapCompletionFlags)
    if (psx.get(v.g.vs_battle_roomData().section9)) |first| {
        var s3 = first;
        var s2: c_int = 1;
        var i: c_int = 0;
        const count: c_int = psx.conv(c_int, @as(usize, v.g.vs_battle_roomData().header.section9Len) / @sizeOf(v._mpdRoomSection9));

        while (i < count) : ({
            i +%= 1;
            s3 = psx.at(s3, 1);
        }) {
            if (v.g.vs_main_stateFlags().unk20 == 0 and v.g.vs_main_stateFlags().unk40 == 0 and s3.unkA != 0) {
                var j: c_int = 0;
                while (j < (v.g.vs_gametime_tickspeed().* *% 2)) : (j +%= 1) {
                    if (@as(i8, @bitCast(s3.unkB)) >= 0) {
                        s3.unkB +%= 1;
                    }
                }
            } else {
                var j: c_int = 0;
                while (j < v.g.vs_gametime_tickspeed().*) : (j +%= 1) {
                    if (s3.unkB != 0) {
                        s3.unkB -%= 1;
                    }
                }
            }

            if (s3.unkB != 0 and s3.unk6 < 0x10) {
                if (s2 != 0) {
                    c.vs_main_memcpy(&marker.matrix, common.view.at(anyopaque, common.view.matrix_offset), @sizeOf(v.MATRIX));
                    v.g.vs_field_triggerMarkerFrameTicks().* = psx.conv(u16, @as(c_int, v.g.vs_field_triggerMarkerFrameTicks().*) +% v.g.vs_gametime_tickspeed().*);
                    if (@as(c_int, v.g.vs_field_triggerMarkerFrameTicks().*) >= 8) {
                        v.g.vs_field_triggerMarkerFrameTicks().* = psx.conv(u16, @as(c_int, v.g.vs_field_triggerMarkerFrameTicks().*) -% 8);
                        s2 = @rem(@as(c_int, v.g.vs_field_roomAnimationTicks().*) >> 3, 5);
                        marker.textureSource.x = psx.conv(i16, (s2 *% 8) +% 0x48);
                        v.g.D_800E85C4().x = psx.conv(i16, (s2 *% 4) +% 4);
                        _ = c.MoveImage(psx.cptr(c.RECT, &marker.textureSource), 0x40, 0x180);
                        _ = c.MoveImage(psx.cptr(c.RECT, v.g.D_800E85C4()), 0, 0x140);
                    }
                    s2 = 0;
                }
                // The C shifts the 16-bit result left by 17 in an int; the top bits are lost, as in C.
                const j: c_int = @as(c_int, c.vs_field_floorHeightAt(@as(c_int, s3.tileX) << 7, @as(c_int, s3.tileZ) << 7)) << 17;
                c.vs_field_drawRoomTriggerMarker(
                    @as(c_int, s3.tileX) << 7,
                    @as(c_int, s3.tileZ) << 7,
                    psx.conv(c_short, j >> 17),
                    @as(c_uint, s3.unkB),
                );
            }
        }
    }
}
