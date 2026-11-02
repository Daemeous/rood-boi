//! The action committed: its costs and hits, the scores, phase 4 (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// D_800F19CC, read at each use as the C does.
fn state() *v.D_800F19CC_t {
    return v.g.D_800F19CC().*.?;
}

/// orig: func_80070F28 (src/BATTLE/BATTLE.PRG/146C.c)
/// Commits the chosen action (`kind`: 0, 2 or another selects its setup). A menu choice left pending (unk298C
/// 0xFF) with `kind` nonzero is dropped instead: back to the actor's turn (func_8006FBCC) for a targeted or plain
/// action, or func_800704D8. Otherwise: link 0, the ability menu's item use counted down (the player's action), the
/// setup, the hits (func_80085B10), the weapon's attack count and the heal count scored, the zoom kind from the
/// action, phase 4, every other actor told, and the screen and sound state for the action.
pub fn commitAction(kind: c_int) callconv(.c) void {
    c.func_8006EF10();
    const pending = state().unk298C;
    c.func_800C16DC();
    c.func_800C06E0();
    c.func_800C0738();
    c.func_800C05B4();
    if (kind != 0) {
        if (pending == 0xFF) {
            if (state().unk8.unk4.unk0.targetActor != 0) {
                c.func_800CB114();
                c.func_800E4C64(@as(c_int, state().unk8.unk4.unk0.targetActor));
                state().unk8.actionIndex = 0;
                c.func_8006FBCC(0);
                return;
            }
            // the action index is not bounded by the C
            if (psx.getBits(psx.at(v.g.actions(), state().unk8.actionIndex), "unk2_0") == 0) {
                c.func_800CB114();
                state().unk8.actionIndex = 0;
                c.func_8006FBCC(0);
                return;
            }
            state().unk8.actionIndex = 0;
            c.func_800704D8();
            return;
        }
    }
    state().unk4 = 1;
    state().unk0 = 0;
    if (state().unk8.unk4.unk0.targetActor == 0) {
        // ROOD_PC guard: the ability menu state is null until the menu first opens; the PS1 reads address 0.
        const ability: *align(1) v.D_800F18EC_t = if (psx.global(?*v.D_800F18EC_t, "D_800F18EC").*) |p| p else psx.lowRam(v.D_800F18EC_t);
        if (ability.unk2 != 7) {
            if (ability.unk2 == 8) {
                _ = c.vs_battle_decreaseMiscCount(@as(c_int, ability.executeAbility.u16[1]));
            }
        }
        ability.unk2 = 0;
    } else {
        c.func_800D82A8(@as(c_int, state().unk8.unk4.unk0.targetActor));
    }
    c.func_800CB114();
    if (kind == 0) {
        c.func_80070CAC();
    } else if (kind == 2) {
        c.func_80070B04();
    } else {
        c.func_800708EC();
    }
    c.func_80085B10(@as(c_int, state().unk8.actionIndex), @ptrCast(&state().unk854[0]), @ptrCast(&state().unk8), 1);

    if (state().unk8.unk4.unk0.targetActor == 0) {
        const index: c_int = state().unk8.actionIndex;
        if ((index == 1 or psx.conv(c_uint, index -% 0xB8) < 0x28) and state().unk8.unk4C[0].unk40 == 0 and
            // the target is not bounded by the C
            psx.at(v.g.vs_battle_actors(), state().unk8.unk4C[0].unk0.targetActor).*.?.unk27 != 0x80)
        {
            const attacks = &v.g.scoreData().weaponAttacks;
            if (v.g.vs_battle_characterState().*.?.equippedWeaponCategory == 0xA) {
                if (attacks[0] <= 0xFFFE) {
                    attacks[0] += 1; // <= 0xFFFE: no wrap
                }
            } else {
                // the category is not bounded by the C
                const count = psx.at(attacks, v.g.vs_battle_characterState().*.?.equippedWeaponCategory);
                if (count.* <= 0xFFFE) {
                    count.* += 1; // <= 0xFFFE: no wrap
                }
            }
        }
    }

    if (c.func_80077F70() >= 0x1C1 and psx.getBits(psx.at(v.g.actions(), state().unk8.actionIndex), "flagsD_4") == 1) {
        state().unk2C0E = 0;
    } else {
        state().unk2C0E = psx.getBits(psx.at(v.g.actions(), state().unk8.actionIndex), "flagsD_4");
    }
    state().unk2C02 = 0;
    state().previousChainInput = 0;
    state().unk2C03 = 0;
    state().unk2C07 = 0;

    if (state().unk8.actionIndex >= 55) {
        psx.global(u8, "D_8005FFAF").* = 0;
        switch (state().unk8.actionIndex) {
            0x4D, 0x60, 0xE0, 0xE1, 0xE2, 0xE3, 0xE8, 0xE9, 0xEA, 0xEB => {
                const score = v.g.scoreData();
                if (score.healCount < 0x3E8) {
                    score.healCount += 1; // < 0x3E8: no wrap
                }
            },
            else => {},
        }
    }

    v.g._cameraMode().* = 4;
    var i: c_int = 0;
    while (i < 16) : (i += 1) {
        if (i != state().unk8.unk4.unk0.targetActor) {
            c.func_8009E5C4(i);
        }
    }
    c.vs_main_setClutState(1);
    c.func_8007B1B8(8, 4, 0, 0, 0);
    c.func_8006C250();
    c.func_80095B70(1);
    _ = c.func_8008B4C8(0);
    c.func_8008B4BC(0);
}
