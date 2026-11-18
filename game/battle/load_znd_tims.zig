//! Splits the TIM block list of a battle graphics pack and loads each image to VRAM
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

inline fn readInt(p: [*]const u8) c_int {
    return @as(*align(1) const c_int, @ptrCast(p)).*;
}

/// orig: _loadZndTims (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Parses the TIM blocks of a ZND pack and uploads each one: CLUT rows to the palette sprite, the rest with LoadImage.
pub fn loadZndTims(data: ?*anyopaque) callconv(.c) void {
    var tim: v.TIM_IMAGE = undefined;
    const base: [*]u8 = @ptrCast(data.?);

    // Four size words at the start of the pack give the sizes of four blocks that follow it (after 16 bytes of
    // sizes, 4-byte units); the images are the first block: a count, then each image as its byte length and its TIM.
    const first_size: c_int = readInt(base);
    if (first_size <= 0) return; // an empty first block (the original would read a null block)
    var cursor: [*]u8 = base + 16;
    const numTims: c_int = readInt(cursor);
    cursor += 4;

    var i: c_int = 0;
    while (i < numTims) : (i +%= 1) {
        const byteOffset: c_int = readInt(cursor);
        cursor += 4;

        c.vs_battle_parseTim(
            @as([*c]c.u_long, @ptrCast(@alignCast(cursor))),
            psx.cptr(c.TIM_IMAGE, &tim),
        );

        if (tim.paddr) |paddr| {
            if (@as(c_int, tim.prect.?.x) >= 768) {
                var line: c_int = tim.prect.?.y;
                var pixels: usize = @intFromPtr(paddr);
                while (line < @as(c_int, tim.prect.?.y) + @as(c_int, tim.prect.?.h)) {
                    const clutIndex: c_uint = psx.conv(c_uint, line -% 224);
                    if (clutIndex < 4) {
                        c.vs_field_copyZonePaletteRow(
                            @as(?*anyopaque, @ptrFromInt(pixels)),
                            psx.conv(c_int, clutIndex),
                            @as(c_int, tim.prect.?.x) -% 768,
                            @as(c_int, tim.prect.?.w),
                        );
                    }
                    line +%= 1;
                    pixels +%= @as(usize, @bitCast(@as(isize, tim.prect.?.w) *% 2));
                }
            } else {
                const rect = tim.prect.?;
                rect.x = psx.conv(i16, @as(c_int, rect.x) -% 320);
                rect.y = psx.conv(i16, @as(c_int, rect.y) +% 256);
                _ = c.LoadImage(psx.cptr(c.RECT, rect), @ptrCast(paddr));
            }
        }

        cursor += @as(usize, psx.conv(u32, byteOffset >> 2)) *% 4;
    }

    _ = c.DrawSync(0);
}
