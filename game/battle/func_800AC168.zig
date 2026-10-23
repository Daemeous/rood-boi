//! Palette effects on a run of 15-bit colour words: fades, tints and the
//! reference-relative blend (src/BATTLE/BATTLE.PRG/40564.c).
const psx = @import("common").psx;

/// orig: func_800AC168 (src/BATTLE/BATTLE.PRG/40564.c)
/// Applies palette effect `mode` to `count` colour words at `colors`, using `reference` as the base colours for mode 4; returns 1 if a word changed.
pub fn func_800AC168(colors: [*c]c_ushort, count: c_int, amount: c_int, mode: c_int, reference: [*c]c_ushort) callconv(.c) c_int {
    var i: c_int = 0;
    var changed: c_int = 0;
    if (count > 0) {
        while (true) {
            // i runs 0..count-1 here, so the index is non-negative.
            const idx: usize = @intCast(i);
            var b: c_int = colors[idx];
            var r: c_int = b & 31;
            var g: c_int = (b >> 5) & 31;
            b = (b >> 10) & 31;
            var color: c_int = 0;
            var baseR: c_int = 0;
            var baseG: c_int = 0;
            var delta: c_int = 0;
            var output: c_int = 0;
            var fade = false;
            switch (mode) {
                0 => {
                    delta = amount *% 2;
                    r -%= delta;
                    if (r < 0) r = 0;
                    g -%= delta;
                    if (g < 0) g = 0;
                    r |= g << 5;
                    if (r != 0) changed = 1;
                    r |= b << 10;
                    output = r | ~@as(c_int, 0x7FFF);
                    colors[idx] = psx.conv(u16, output);
                },
                1 => {
                    delta = amount *% 4;
                    fade = true;
                },
                2 => {
                    output = 0xFFFF;
                    colors[idx] = psx.conv(u16, output);
                },
                3 => {
                    r +%= amount;
                    if (r >= 32) r = 31;
                    g +%= amount *% 2;
                    if (g >= 32) g = 31;
                    b +%= amount *% 2;
                    if (b >= 32) b = 31;
                    r |= g << 5;
                    r |= b << 10;
                    output = r | ~@as(c_int, 0x7FFF);
                    colors[idx] = psx.conv(u16, output);
                },
                4 => {
                    color = reference[idx];
                    r -%= amount *% 2;
                    baseR = color & 31;
                    baseG = (color >> 5) & 31;
                    color = (color >> 10) & 31;
                    if (r < baseR) r = baseR;
                    g -%= amount *% 2;
                    if (g < baseG) g = baseG;
                    b -%= amount *% 2;
                    if (b < color) b = color;
                    r |= g << 5;
                    delta = b << 10;
                    color = reference[idx];
                    r |= delta;
                    if (r != (color & 0x7FFF)) {
                        changed = 1;
                    }
                    output = (color & 0x8000) | r;
                    colors[idx] = psx.conv(u16, output);
                },
                5 => {
                    // Shares the fade tail with mode 1 (the C's goto fade).
                    delta = amount;
                    fade = true;
                },
                6 => {
                    output = 0x8000;
                    colors[idx] = psx.conv(u16, output);
                },
                else => {},
            }
            if (fade) {
                b -%= delta;
                if (b < 0) b = 0;
                r -%= delta;
                if (r < 0) r = 0;
                g -%= delta;
                if (g < 0) g = 0;
                r |= g << 5;
                r |= b << 10;
                if (r != 0) changed = 1;
                output = r | ~@as(c_int, 0x7FFF);
                colors[idx] = psx.conv(u16, output);
            }
            i +%= 1;
            if (i >= count) break;
        }
    }
    return changed;
}
