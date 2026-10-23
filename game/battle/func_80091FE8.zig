//! Scrolls a tiled image one step: advances its tick, moves its rect and copies the visible frame and its half-resolution twin (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80091FE8 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Advances a scrolling image's tick and, when it is due, moves its rect and copies its frame and half-size twin.
pub fn func_80091FE8(arg0: ?*v.func_80091FE8_t) callconv(.c) c_int {
    const self = arg0.?;
    var source: v.RECT = undefined;
    var halfSource: v.RECT = undefined;

    self.tick +%= 1;
    if (@as(c_int, self.tick) > @as(c_int, self.delay)) {
        self.rect.x = psx.conv(i16, @as(c_int, self.rect.x) -% 320);
        self.rect.y = psx.conv(i16, @as(c_int, self.rect.y) +% 256);
        self.tick = 0;
        if (self.scrollStep == 0) {
            self.frame +%= 1;
            if (@as(c_int, self.frame) > @as(c_int, self.frameCount)) {
                self.frame = 1;
            }
            // (u_int) columns: the frame index and the division are unsigned in C.
            const cols: c_uint = self.columns;
            const cur: c_uint = self.frame;
            source.x = psx.conv(i16, psx.conv(c_uint, self.rect.x) +%
                psx.conv(c_uint, self.rect.w) *% (cur % cols));
            source.y = psx.conv(i16, psx.conv(c_uint, self.rect.y) +%
                psx.conv(c_uint, self.rect.h) *% (cur / cols));
            source.w = self.rect.w;
            source.h = self.rect.h;
        } else {
            self.frame = psx.conv(u8, @as(c_int, self.frame) +% self.scrollStep);
            if (@as(c_int, self.frame) >= @as(c_int, self.rect.h)) {
                self.frame = 0;
            }
            const cur: c_int = self.frame;
            source.x = psx.conv(i16, @as(c_int, self.rect.x) +%
                @divTrunc(@as(c_int, self.rect.w), 2));
            source.y = psx.conv(i16, @as(c_int, self.rect.y) +% cur);
            source.w = psx.conv(i16, @divTrunc(@as(c_int, self.rect.w), 2));
            source.h = psx.conv(i16, @as(c_int, self.rect.h) -% cur);
        }

        const halfDestX: c_int = @divTrunc(@as(c_int, psx.conv(i16, @rem(@as(c_int, self.rect.x), 64))), 2) -% 64;
        const destX: c_int = halfDestX +% (@as(c_int, self.rect.x) & 0x3C0);
        var destY: c_int = @divTrunc(@as(c_int, psx.conv(i16, @rem(@as(c_int, self.rect.y), 256))), 2) +%
            @as(c_int, psx.conv(i16, @as(c_int, self.rect.y) & 0x100));

        const halfSourceX: c_int = @divTrunc(@as(c_int, psx.conv(i16, @rem(@as(c_int, source.x), 64))), 2) -% 64;
        halfSource.x = psx.conv(i16, halfSourceX +% (@as(c_int, source.x) & 0x3C0));
        halfSource.y = psx.conv(i16, @divTrunc(@as(c_int, psx.conv(i16, @rem(@as(c_int, source.y), 256))), 2) +%
            (@as(c_int, source.y) & 0x100));
        halfSource.w = psx.conv(i16, @divTrunc(@as(c_int, source.w), 2));
        halfSource.h = psx.conv(i16, @divTrunc(@as(c_int, source.h), 2));

        _ = c.MoveImage(psx.cptr(c.RECT, &source), @as(c_int, self.rect.x), @as(c_int, self.rect.y));
        _ = c.MoveImage(psx.cptr(c.RECT, &halfSource), destX, destY);

        if (self.scrollStep != 0 and self.frame != 0) {
            source.y = self.rect.y;
            source.h = self.frame;
            const wrapY: c_int = @as(c_int, self.rect.y) +% @as(c_int, self.rect.h) -% @as(c_int, self.frame);
            destY = @divTrunc(@rem(wrapY, 256), 2) +% (wrapY & 0x100);
            halfSource.y = psx.conv(i16, @divTrunc(@as(c_int, psx.conv(i16, @rem(@as(c_int, source.y), 256))), 2) +%
                (@as(c_int, source.y) & 0x100));
            halfSource.h = psx.conv(i16, @divTrunc(@as(c_int, source.h), 2));
            _ = c.MoveImage(psx.cptr(c.RECT, &source), @as(c_int, self.rect.x), wrapY);
            _ = c.MoveImage(psx.cptr(c.RECT, &halfSource), destX, destY);
        }

        self.rect.x = psx.conv(i16, @as(c_int, self.rect.x) +% 320);
        self.rect.y = psx.conv(i16, @as(c_int, self.rect.y) -% 256);
    }
    return 1;
}
