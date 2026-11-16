//! Splits the TIM block list of a battle graphics pack and loads each image to VRAM
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _loadZndTims (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Parses the TIM blocks of a ZND pack and uploads each one: CLUT rows to the palette sprite, the rest with LoadImage.
pub fn loadZndTims(data: ?*anyopaque) callconv(.c) void {
    var tim: v.TIM_IMAGE = undefined;
    var timPtrs: [4]u32 = undefined;
    const base: u32 = psx.addr(data.?);
    var byteOffset: c_int = 0;
    var sizeAddr: u32 = base;

    // Four size words at the start of the pack; entry k is the address of block k's data, or 0 if empty.
    var entry: usize = 0;
    while (entry < 4) : (entry += 1) {
        const size: c_int = @as(*align(1) c_int, @ptrFromInt(@as(usize, sizeAddr))).*;
        if (size > 0) {
            const n: c_int = @divTrunc(byteOffset, 4) +% 4;
            timPtrs[entry] = base +% (psx.conv(u32, n) *% 4);
        } else {
            timPtrs[entry] = 0;
        }
        byteOffset +%= size;
        sizeAddr +%= 4;
    }

    var dataAddr: u32 = timPtrs[0];
    const numTims: c_int = @as(*align(1) c_int, @ptrFromInt(@as(usize, dataAddr))).*;
    dataAddr +%= 4;

    var i: c_int = 0;
    while (i < numTims) : (i +%= 1) {
        byteOffset = @as(*align(1) c_int, @ptrFromInt(@as(usize, dataAddr))).*;
        dataAddr +%= 4;

        c.vs_battle_parseTim(
            @as([*c]c.u_long, @ptrFromInt(@as(usize, dataAddr))),
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

        dataAddr +%= psx.conv(u32, byteOffset >> 2) *% 4;
    }

    _ = c.DrawSync(0);
}
