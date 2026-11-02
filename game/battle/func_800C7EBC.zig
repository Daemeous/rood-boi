//! Blends a font glyph into a 16-bit image (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800C7EBC (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Reads glyph `glyph`'s 12x12 4-bit cell from VRAM (StoreImage) and blends its coloured pixels (palette D_800EB98C) into `destination` (rows `stride` apart) by `alpha` / 65536.
pub fn func_800C7EBC(destination_arg: [*c]u16, glyph: c_uint, stride: c_int, alpha: c_uint) callconv(.c) void {
    var destination = destination_arg;
    var rect: v.RECT = undefined;
    var pixels: [18]c_uint = undefined;
    var packed_pixels: u16 = undefined; // set at every fourth column before it is shifted
    const inverse: c_uint = 0x10000 -% alpha;
    rect.x = psx.conv(i16, (glyph % 21) * 3 + 0x340);
    rect.y = psx.conv(i16, (glyph / 21) *% 12);
    rect.w = 3;
    rect.h = 12;
    _ = c.StoreImage(@ptrCast(&rect), &pixels);
    _ = c.DrawSync(0);
    var row: c_int = 0;
    while (row < 12) {
        var column: c_int = 0;
        var output = destination;
        while (column < 12) : ({
            column += 1;
            output += 1;
        }) {
            if ((column & 3) != 0) {
                packed_pixels >>= 4;
            } else {
                // ROOD_PC_LP64: plain byte arithmetic on the stack buffer.
                const bytes: [*]const u8 = @ptrCast(&pixels);
                packed_pixels = @as(*align(1) const u16, @ptrCast(bytes + @as(usize, @intCast(((column >> 2) + row * 3) << 1)))).*;
            }
            const index: c_uint = packed_pixels & 15;
            if (index != 0) {
                const previous: c_uint = output[0];
                const source: c_uint = v.g.D_800EB98C()[index];
                output[0] = psx.conv(u16, (((((previous & 31) *% inverse) +% ((source & 31) *% alpha)) >> 16) & 31) |
                    (((((previous & 0x3E0) *% inverse) +% ((source & 0x3E0) *% alpha)) >> 16) & 0x3E0) |
                    (((((previous & 0x7C00) *% inverse) +% ((source & 0x7C00) *% alpha)) >> 16) & 0x7C00));
            }
        }
        row += 1;
        destination = @ptrFromInt(@intFromPtr(destination) +% @as(usize, @bitCast(@as(isize, stride) * 2)));
    }
}
