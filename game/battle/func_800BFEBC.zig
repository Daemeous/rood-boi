//! Eases a frame/duration ratio through a curve (linear, sine, cosine, or a powered curve)
//! (src/BATTLE/BATTLE.PRG/573B8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_573B8");
const c = @import("c_573B8");

/// orig: func_800BFEBC (src/BATTLE/BATTLE.PRG/573B8.c)
/// Eases `frame` over `duration` (clamped to it) through curve `mode`, returning a 4.12 fraction.
pub fn func_800BFEBC(mode: c_short, frame: c_short, duration: c_short) callconv(.c) c_int {
    const position: c_int = if (frame > duration) duration else frame;
    const dur: c_int = duration;
    switch (@as(c_int, mode)) {
        0 => return @divTrunc(position << 12, dur),
        1 => return c.rsin(@divTrunc(position << 10, dur)),
        2 => return (c.rcos(@divTrunc(position << 11, dur) +% 0x800) +% 0x1000) >> 1,
        3 => return c.rsin(@divTrunc(position << 10, dur) -% 0x400) +% 0x1000,
        else => {
            // mode -= 4 on a short: the result is truncated to short.
            const idx: i16 = psx.conv(i16, @as(c_int, mode) -% 4);
            const row = psx.at(v.g.D_800F4B28(), idx).*;
            const power: c_int = row[1];
            var value: c_int = @divTrunc(position << 12, dur);
            if (power != 0) {
                if (power > 0) {
                    var i: c_int = 0;
                    while (i < power) : (i += 1) {
                        value = (value *% value) >> 12;
                    }
                } else {
                    value = 0x1000 -% value;
                    const magnitude: c_int = -%power;
                    var i: c_int = 0;
                    while (i < magnitude) : (i += 1) {
                        value = (value *% value) >> 12;
                    }
                    value = 0x1000 -% value;
                }
            }
            const kind: u8 = @bitCast(row[0]);
            switch (@as(c_int, kind)) {
                1 => return c.rsin(@divTrunc(value, 4)),
                2 => return (c.rcos(@divTrunc(value, 2) +% 0x800) +% 0x1000) >> 1,
                3 => return c.rsin(@divTrunc(value, 4) -% 0x400) +% 0x1000,
                else => return value,
            }
        },
    }
}
