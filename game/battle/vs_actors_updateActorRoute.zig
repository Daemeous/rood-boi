//! Two-phase actor movement step: starts a move, then completes it on the next pass
//! (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_updateActorRoute (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Advances an actor's movement phase: phase 1 starts the move, phase 2 completes it once the wait counter clears.
pub fn vs_actors_updateActorRoute(arg0: c_int, arg1: ?*v.D_800F4538_t) callconv(.c) void {
    const p = arg1.?;

    switch (@as(c_int, psx.getBits(p, "unk5AC_0"))) {
        1 => {
            if (p.unk0.unk18 == 0) {
                if (psx.getBits(&p.unk0, "unkA_5") != 0) {
                    c.vs_actors_playActorAnimation(arg0, 0x1F, 0, 8);
                } else {
                    c.vs_actors_playActorAnimation(arg0, @as(c_int, p.unk6E5), 0, 8);
                }

                if (psx.getBits(p, "unk5AC_2") == 0) {
                    c.vs_actors_translateActorToAtRate(arg0, @ptrCast(&p.unk5EC), @as(c_int, p.unk5C4));
                } else {
                    c.vs_actors_translateActorToOverTime(arg0, @ptrCast(&p.unk5EC), @as(c_int, p.unk5C4));
                }

                psx.setBits(p, "unk5AC_0", 2);
            }
        },
        2 => {
            if (p.unk0.unk1A == 0) {
                c.vs_actors_playActorAnimation(arg0, 1, 0, 8);

                var temp_a1: c_int = @as(c_int, psx.conv(u16, p.unk5EC.pad));

                if (temp_a1 != 0x8000) {
                    temp_a1 &= 0xFFF;
                    if (psx.getBits(p, "unk5AC_2") == 0) {
                        c.vs_actors_turnActorToAtRate(arg0, psx.conv(c_short, temp_a1), @as(c_int, p.unk5C6));
                    } else {
                        c.vs_actors_turnActorToOverTime(arg0, psx.conv(u16, temp_a1), @as(c_int, p.unk5C6));
                    }
                }

                psx.setBits(p, "unk5AC_0", 0);
            }
        },
        else => {},
    }
}
