//! Probes the floor height under a point offset from an actor's position, using the
//! 3A1A0 unit's collision tables (src/BATTLE/BATTLE.PRG/3A1A0.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A6EE8 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Height at the point arg1/arg2 from `pos` for probe kind arg3; -3000 when there is no floor.
pub fn func_800A6EE8(arg0: ?*v.SVECTOR, arg1: c_int, arg2: c_int, arg3: c_int) callconv(.c) c_int {
    const p = arg0.?;
    // sp.vy is uninitialised in the original; it is zeroed here.
    var sp = std.mem.zeroes(v.SVECTOR);

    const x: c_int = arg1 +% @as(c_int, p.vx);
    const z: c_int = arg2 +% @as(c_int, p.vz);

    sp.vx = psx.conv(i16, x);
    sp.vz = psx.conv(i16, z);

    const ent = c.func_800A8D64(psx.cptr(c.SVECTOR, &sp), 0);

    if (ent != null) {
        v.g.D_800F49F0().* = @ptrCast(ent);

        var h: c_int = 0;
        if (arg3 == 0) {
            h = @as(c_int, c.func_8008DC7C(x, z));
        } else {
            h = @as(c_int, c.func_8008DA24(x, z));
        }

        if (h >= 0) {
            // Sign-extends the low 15 bits of h.
            h = (h << 17) >> 17;

            var r: c_int = 0;
            if (arg3 == 2) {
                r = c.func_800B13CC(@divTrunc(x, 128), @divTrunc(z, 128), @as(c_int, p.vy));
            } else {
                r = c.func_800A92B8(@divTrunc(x, 128), @divTrunc(z, 128));
            }

            if (r != 0) {
                h = r;
            }

            if (((ent.* >> 17) & 1) != 0 and h >= 0) {
                h = 3000;

                if (arg3 == 3) {
                    const idx: u32 = ent.* & 0x1F;
                    const table_los = @as(c_int, v.g.D_800E9278()[idx]);

                    if (table_los == 0x14) {
                        h = 0;
                    }
                }
            }

            if (arg3 != 2 and arg3 != 4) {
                const los = c.func_800A9378(x, @as(c_int, p.vy), z, 0);

                r = h;

                if (los != 0 and los < h) {
                    h = los;
                }

                if (arg3 == 3 and h < @as(c_int, p.vy) -% 64) {
                    h = r;
                }
            }

            return h;
        }
    }

    if (arg3 == 3) {
        return @as(c_int, p.vy);
    }

    return -3000;
}
