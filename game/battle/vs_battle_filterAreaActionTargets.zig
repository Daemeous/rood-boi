//! Resolves the action's target list: keeps the targets that qualify, records their hits
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_filterAreaActionTargets (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Rebuilds the action's target list, keeping the qualifying targets and recording each one's hit.
pub fn vs_battle_filterAreaActionTargets() callconv(.c) void {
    const d = v.g.vs_battle_actionState().*.?;
    // uninitialised in the original: written by vs_battle_previewActionAgainstLimb before it is read
    var result: v.D_800F19CC_t2 = std.mem.zeroes(v.D_800F19CC_t2);

    d.unk2990 = d.unk2998;
    c.func_800C06E0();
    if (d.unk2990.pad != 0) {
        c.func_800C16DC();
        c.func_800C06E0();
        c.func_800C0738();
        c.func_800C05B4();
        if (psx.getBits(psx.at(v.g.actions(), d.unk8.actionIndex), "unk2_0") == 0) {
            c.vs_menu_frameworkReleaseMenuRows();
            c.vs_battle_beginPostActionRecovery(0);
            return;
        }
        c.vs_battle_returnToBattleActionMenu();
        return;
    }

    const action: c_int = @as(c_int, d.unk8.actionIndex);
    d.areaOrigin = d.unk2990;
    c.func_800C0700(@ptrCast(&d.unk29B0));

    var kept: c_int = 0;
    var i: c_int = 0;
    while (i < @as(c_int, d.previewCount)) : (i +%= 1) {
        if (v.g.vs_battle_categoryTargetsBodyParts()[psx.getBits(psx.at(v.g.actions(), action), "unk2_4")] != 0) {
            if (c.vs_battle_isPointInTargetVolume(@ptrCast(&d.unk29B0), @ptrCast(psx.at(&d.previews, i))) != 0) {
                c.vs_main_memcpy(@ptrCast(psx.at(&d.previews, kept)), @ptrCast(psx.at(&d.previews, i)), 24);
                _ = c.vs_battle_previewActionAgainstLimb(
                    action,
                    @as(c_int, d.unk8.primaryTarget.target.targetActor),
                    @as(c_int, psx.at(&d.previews, kept).unk9),
                    @as(c_int, psx.conv(i8, psx.at(&d.previews, kept).unkA)),
                    &result,
                );
                c.vs_battle_populateActionTargetPreview(@ptrCast(psx.at(&d.previews, kept)), @ptrCast(&result), @ptrCast(&result.targets[0]));
                kept +%= 1;
            }
        } else if (c.vs_battle_isPointInTargetVolume(@ptrCast(&d.unk29B0), @ptrCast(psx.at(&d.previews, i))) != 0) {
            psx.at(&d.previews, kept).unk8 = 16;
            const av: *v.actionTargetContextView = @ptrCast(d);
            psx.at(&av.targets, kept).limb = -1;
            psx.at(&d.previews, kept).unk9 = psx.at(&d.previews, i).unk9;
            psx.at(&d.previews, kept).unk0 = psx.at(&d.previews, i).unk0;
            const slot: u8 = psx.at(&d.previews, kept).unk9;
            const stats = psx.get(v.g.vs_battle_actors()[slot].?.stats).?;
            _ = c.vs_battle_previewActionAgainstLimb(
                action,
                @as(c_int, d.unk8.primaryTarget.target.targetActor),
                @as(c_int, slot),
                @as(c_int, psx.getBits(stats, "unk36")),
                &result,
            );
            c.vs_battle_populateActionTargetPreview(@ptrCast(psx.at(&d.previews, kept)), @ptrCast(&result), @ptrCast(&result.targets[0]));
            kept +%= 1;
        }
    }

    d.previewCount = psx.conv(u16, kept);
    c.vs_battle_setSphereTargets(kept, &d.previews[0], 1);
    v.g._cameraMode().* = 3;
}
