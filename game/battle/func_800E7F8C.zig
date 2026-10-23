//! Steps the battle state flag 0xA6 machine for `arg0` and its D_800F5920 timers (src/BATTLE/BATTLE.PRG/7EE98.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_7EE98");
const c = @import("c_7EE98");

/// orig: func_800E7F8C (src/BATTLE/BATTLE.PRG/7EE98.c)
/// Runs one step of the state flag 0xA6 machine on `arg0`, then stores the flag back.
pub fn func_800E7F8C(arg0: ?*v.func_800E78F4_t) callconv(.c) c_int {
    const self = arg0.?;

    if (c.func_800E78F4(psx.cptr(c.func_800E78F4_t, self)) != 0) {
        c.func_800D836C(psx.cptr(c.func_800E78F4_t, self));
    }

    var state: u8 = c.vs_battle_getStateFlag(0xA6);
    const temp = v.g.D_800F5920().*.?;

    if (temp.unkC > 0) {
        temp.unkC -%= 1;
    }

    switch (state) {
        0 => {
            if (temp.unk16 != 0) {
                temp.unk16 = 0;
                self.unk470 = temp.unk17;
            }

            temp.unkA = 0;
            c.func_800E7960(@ptrCast(self));

            if (temp.unkE != 0 and temp.unkC == 0 and psx.get(self.unk5C).?.limbs[4].hp >= 2) {
                temp.unkC = 900;
                temp.unkE = 0;
                temp.unk17 = psx.conv(u8, self.unk470);
                state = 1;
                self.unk470 = 0;
                temp.unk16 = 1;
            }
        },
        4 => {
            temp.unk1C = 0;
            c.func_800D82CC(psx.cptr(c.func_800E78F4_t, self));

            if (c.func_800E7698(@ptrCast(self)) != 0) {
                if (temp.unkA != 0) {
                    const temp_a1 = psx.get(self.unk54).?;

                    if (psx.get(temp_a1.unk3C).?.limbs[4].hp >= 2) {
                        temp_a1.unkC = 4;
                        temp_a1.unkE = 0;
                        // setVector expanded in the object: x, then y, then z, each read from D_800F4538[0].
                        const cam = v.g.D_800F4538()[0].?;
                        temp_a1.unk10.vx = cam.unk0.position.vx;
                        temp_a1.unk10.vy = psx.conv(i16, @as(c_int, cam.unk0.position.vy) -% (@as(c_int, cam.menuCameraHeightOffset) >> 1));
                        temp_a1.unk10.vz = v.g.D_800F4538()[0].?.unk0.position.vz;
                        temp_a1.unk9 = 24;
                        temp_a1.unk8 = 0x80;
                    }

                    temp.unk12 = self.unk4E;
                    self.unk470 = temp.unk17;
                    temp.unk16 = 0;
                } else {
                    state = 6;
                }

                v.g.D_800F591C().* = 0;
                temp.unkE = 0;
                v.g.D_800F5918().* = 0;
            }
        },
        else => {},
    }

    c.vs_battle_setStateFlag(0xA6, state);
    return 1;
}
