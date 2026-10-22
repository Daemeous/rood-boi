//! Advances the battle row animations: steps each active row toward its resting
//! position and deactivates it when it leaves the visible band (src/BATTLE/BATTLE.PRG/58578.c).
const common = @import("common");
const psx = @import("common").psx;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: func_800C20B4 (src/BATTLE/BATTLE.PRG/58578.c)
/// Steps every active row of the battle layout one animation frame, then culls rows that left the band.
pub fn func_800C20B4() callconv(.c) void {
    const base = v.g.D_800EB9B8().*.?;
    const steps = v.g.vs_battle_rowAnimationSteps();

    var i: c_int = 0;
    while (i < 24) : (i += 1) {
        const s1 = &base.unk990[@intCast(i)];
        const s0 = &s1.unk4;

        const v1: c_int = s1.unk0;
        if (v1 == 0) continue;

        switch (v1) {
            0 => {},
            2 => {
                const t: c_int = s0.*;

                if (t >= 160) {
                    var k: c_int = 0;
                    while (k < 16) : (k += 1) {
                        if (@as(c_int, steps[@intCast(k)]) +% 0xF8 >= t) break;
                    }

                    if (k != 0) {
                        s0.* = psx.conv(i16, @as(c_int, steps[@intCast(k - 1)]) +% 0xF8);
                    } else {
                        s0.* = 0xF8;
                        s1.unk0 = 1;
                    }
                } else {
                    s0.* = psx.conv(i16, t -% 0x20);
                }
            },
            3 => {
                var t: c_int = s0.*;

                if (t >= 0xA0) {
                    s0.* = psx.conv(i16, t +% 0x20);
                } else {
                    var k: c_int = 0;
                    while (k < 16) : (k += 1) {
                        if (t +% 4 >= -%@as(c_int, steps[@intCast(k)])) break;
                    }

                    if (k != 0) {
                        t = -%@as(c_int, steps[@intCast(k - 1)]);
                    } else {
                        t = 0;
                        s1.unk0 = 1;
                    }

                    s0.* = psx.conv(i16, t -% 4);
                }
            },
            else => {},
        }

        if (psx.conv(c_uint, @as(c_int, s0.*) +% 0x7E) < 0x1BE) {
            c.func_800C1DC4(psx.cptr(c.D_800EB9B8_unk990, s1));
        } else {
            s1.unk0 = 0;
        }
    }
}
