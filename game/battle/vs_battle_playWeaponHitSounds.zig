//! Plays the attack sound effects for each pending hit entity in the active action
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// Pans sound effect `code` (sfx bank 0x7E) at the hit position `vec`.
fn panSound(code: c_int, vec: *v.SVECTOR) void {
    c.vs_main_panSfx(0x7E, code, psx.cptr(c.SVECTOR, vec));
}

/// orig: vs_battle_playWeaponHitSounds (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// For each not-yet-resolved hit of the current action, plays the weapon-category sound at the target's position.
pub fn vs_battle_playWeaponHitSounds(slot: c_int) callconv(.c) c_int {
    var sp10: v.func_8006EBF8_t = undefined;
    // sp20.pad is never written in the C and vs_main_panSfx loads the whole word into the GTE: zeroed here and in the C under ROOD_PC.
    var sp20: v.SVECTOR = .{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };

    const base = v.g.vs_battle_actionState().*.?;
    const temp = &base.unk854[@intCast(base.unk0 & 3)];

    var i: c_int = 0;
    while (i < @as(c_int, temp.unk4A)) : (i +%= 1) {
        // unk4A may exceed the 30 entries of unk4C: the C does not check it.
        const entry = psx.at(&temp.unk4C, i);
        if (entry.unk40 == 0) {
            _ = c.vs_actors_actorPlacement(@as(c_int, entry.unk0.targetActor), &sp10);

            sp20.vx = sp10.unk0.unk4.vx;
            sp20.vz = sp10.unk0.unk4.vz;
            sp20.vy = sp10.unk0.unk4.vy;

            switch (entry.unk0.unk3) {
                0, 1, 2, 4 => switch (psx.at(v.g.vs_battle_actors(), slot).*.?.equippedWeaponCategory) {
                    1 => {
                        panSound(0x47, &sp20);
                        panSound(0x48, &sp20);
                    },
                    2 => {
                        panSound(0x49, &sp20);
                        panSound(0x4A, &sp20);
                    },
                    3 => {
                        panSound(0x4B, &sp20);
                        panSound(0x4C, &sp20);
                    },
                    4 => {
                        panSound(0x50, &sp20);
                        panSound(0x51, &sp20);
                    },
                    5 => {
                        panSound(0x52, &sp20);
                        panSound(0x53, &sp20);
                    },
                    6 => {
                        panSound(0x4D, &sp20);
                    },
                    7 => {
                        panSound(0x4E, &sp20);
                        panSound(0x4F, &sp20);
                    },
                    8 => {
                        panSound(0x54, &sp20);
                        panSound(0x55, &sp20);
                    },
                    10 => {
                        panSound(0x5B, &sp20);
                        panSound(0x5C, &sp20);
                    },
                    9 => {
                        panSound(0x69, &sp20);
                        panSound(0x6A, &sp20);
                    },
                    else => {},
                },
                3 => panSound(0x33, &sp20),
                else => {},
            }
        }
    }

    return 1;
}
