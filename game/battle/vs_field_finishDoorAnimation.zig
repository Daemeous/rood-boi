//! Toggles a room section and its linked section (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_finishDoorAnimation (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Toggles a room section and its linked section, returning 0 if the section's lock is held.
pub fn vs_field_finishDoorAnimation(arg0: ?*v._mpdRoomSectionA) callconv(.c) c_int {
    const section = arg0.?;
    if (c.vs_field_isRoomEffectActive(@as(c_int, section.unk0.unk8.lock.unk1)) != 0) {
        return 0;
    }
    if (section.unkE == 0) {
        c.vs_field_toggleDoorCollision(psx.cptr(c._mpdRoomSectionA, section));
    }
    section.unk12 = 0;
    section.unkE = psx.conv(i16, @as(c_int, @intFromBool(section.unkE == 0)));
    if (section.unk10 != -1) {
        const p = psx.get(v.g.vs_battle_roomData().sectionA).?;
        if (psx.at(p, section.unk10).unk0.unkA != 0) {
            if (psx.at(p, section.unk10).unkE == 0) {
                c.vs_field_toggleDoorCollision(psx.cptr(c._mpdRoomSectionA, psx.at(p, section.unk10)));
            }
            psx.at(p, section.unk10).unkE = psx.conv(i16, @as(c_int, @intFromBool(psx.at(p, section.unk10).unkE == 0)));
            psx.at(p, section.unk10).unk12 = 0;
        }
    }
    return 1;
}
