//! Room section setup: marks the section set up and runs its key or sigil effect (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_beginDoorToggle (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets up a room section once: runs its key or sigil effect, then its follow-up call; returns 1 if it ran, 0 if already set up.
pub fn vs_field_beginDoorToggle(arg0: ?*v._mpdRoomSectionA) callconv(.c) c_int {
    const section = arg0.?;
    if (section.unk12 != 0) return 0;

    const temp_a0: c_uint = psx.conv(c_uint, psx.conv(u16, section.keyOrSigil));
    section.unk12 = 1;
    if ((temp_a0 -% 0x1CA) < 7) {
        c.vs_field_markSigilDoorUnlocked(@as(c_int, psx.conv(i16, temp_a0)) -% 0x1CA);
    }
    if (section.unkE == 0) {
        section.unk13 = 8;
        c.vs_field_queueGimmickAnimation(3, psx.cptr(c._mpdRoomSectionA, section));
        return 1;
    }
    _ = c.vs_field_startDoorAnimation(psx.cptr(c._mpdRoomSectionA, section));
    return 1;
}
