//! Finds the entry of func_8008D2C0's list that a point meets under mode arg3 and returns its height
//! (src/BATTLE/BATTLE.PRG/40564.c).
const psx = @import("common").psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800A9378 (src/BATTLE/BATTLE.PRG/40564.c)
/// Looks up the entries of func_8008D2C0 around (arg0, arg2) under mode arg3, records the matched entry's id, and returns its height or 0.
pub fn func_800A9378(arg0: c_int, arg1: c_int, arg2: c_int, arg3: c_int) callconv(.c) c_int {
    v.g.D_800F49F4().* = 0;
    var sp10: [4]v.func_8008D2C0_t = undefined;
    const count = c.func_8008D2C0(@ptrCast(&sp10));

    if (count == 0) {
        return 0;
    }

    var i: c_int = 0;
    while (i < count) : (i +%= 1) {
        const e = psx.at(&sp10, i);
        const x: c_int = e.x;
        const y: c_int = e.y;
        const z: c_int = e.z;
        const id: c_int = e.id;

        if (arg0 >= x + 64 or arg0 < x - 64) {
            continue;
        }

        if (arg2 >= z + 64 or arg2 < z - 64) {
            continue;
        }

        switch (arg3) {
            0 => {
                if (y < arg1 -% 256) {
                    continue;
                }
            },
            1 => {
                if (y < arg1 -% 256) {
                    return y + 96;
                }
                continue;
            },
            2 => {
                if (y >= arg1 or y < arg1 -% 128) {
                    continue;
                }
            },
            3 => {
                if (y >= arg1 or y < arg1 -% @as(c_int, v.g.D_800F4B00().*)) {
                    continue;
                }
                return y + 96;
            },
            else => {},
        }

        v.g.D_800F49F4().* = id + 2;
        return y;
    }

    return 0;
}
