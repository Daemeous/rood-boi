//! The action's release: its effect aimed, the ability window opened (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// D_800F19CC, read at each use as the C does.
fn state() *v.D_800F19CC_t {
    return v.g.D_800F19CC().*.?;
}

/// orig: func_80070278 (src/BATTLE/BATTLE.PRG/146C.c)
/// Releases the current action slot: state flag 0xB8 for an action 0x16..0x35 on actor 0 with no unk40; the first
/// link aims its effect (func_8009DD00 for a plain action, which sets the ability window from func_8009F8DC; a
/// Break Art, battle ability or other at tick speed 4 and no window), a chain link its chain ability (an action
/// below 0x28) or func_8009EE9C, with the window. Then three sound stops, the name window closed unless the
/// charging skip holds it, and the camera mode (the action phase) 7.
pub fn func_80070278() callconv(.c) void {
    state().battleAbilityTargetWindow = 0;
    state().battleAbilityTimer = 0;
    const slot = &state().unk854[state().unk0 & 3];
    if (psx.conv(c_uint, @as(c_int, slot.actionIndex)) -% 0x16 < 0x20 and slot.unk4.unk40 == 0 and
        slot.unk4.unk0.targetActor == 0)
    {
        c.vs_battle_setStateFlag(0xB8, 1);
    }
    const link: c_int = @bitCast(state().unk0);
    if (link == 0) {
        // the action index is not bounded by the C
        if (psx.getBits(psx.at(v.g.actions(), slot.actionIndex), "unk2_0") == 0) {
            c.func_8009DD00(@as(c_int, slot.unk4.unk0.targetActor), psx.cptr(c.SVECTOR, &state().unk8.unk844), @as(c_int, slot.unk48));
            state().battleAbilityTargetWindow = psx.conv(u16, c.func_8009F8DC(@as(c_int, slot.unk4.unk0.targetActor)) *% 2);
        } else {
            if (slot.actionIndex >= 0x8D) {
                if (slot.actionIndex < 0xB8) {
                    c.func_8009F298(@as(c_int, slot.unk4.unk0.targetActor), psx.cptr(c.SVECTOR, &state().unk8.unk844), 1);
                } else if (slot.actionIndex < 0xE0) {
                    c.func_8009EFEC(@as(c_int, slot.unk4.unk0.targetActor), psx.cptr(c.SVECTOR, &state().unk8.unk844), 1);
                } else {
                    c.func_8009E2E0(@as(c_int, slot.unk4.unk0.targetActor), psx.cptr(c.SVECTOR, &state().unk8.unk844), 1);
                }
            } else {
                c.func_8009E2E0(@as(c_int, slot.unk4.unk0.targetActor), psx.cptr(c.SVECTOR, &state().unk8.unk844), 1);
            }
            state().battleAbilityTargetWindow = 0;
            v.g.vs_gametime_tickspeed().* = 4;
        }
    } else {
        if (slot.actionIndex < 0x28) {
            // mappedChainAbilities[unk2C03 - 1]: the C does not check the index
            const ability = psx.at(&v.g.vs_main_settings().mappedChainAbilities, @as(c_int, state().unk2C03) - 1).*;
            c.func_8009EC9C(@as(c_int, slot.unk4.unk0.targetActor), psx.cptr(c.SVECTOR, &state().unk8.unk844), @as(c_int, ability) - 0x16, link);
        } else {
            c.func_8009EE9C(@as(c_int, slot.unk4.unk0.targetActor), psx.cptr(c.SVECTOR, &state().unk8.unk844), 0);
        }
        state().battleAbilityTargetWindow = psx.conv(u16, c.func_8009F8DC(@as(c_int, slot.unk4.unk0.targetActor)) *% 2);
    }
    c.func_80045D64(0x7E, 0x34);
    c.func_80045D64(0x7E, 0x35);
    c.func_80045D64(0x7E, 0x36);
    if (c.pc_charge_skip_hold() == 0) {
        c.func_800CB50C();
    }
    state().unk2C03 = 0;
    v.g._cameraMode().* = 7;
}
