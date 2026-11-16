//! Locked-door check for a room section: plays the unlock or lock message and sound (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_interactWithLockedRoomObject (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the lock state of a room section: unlocks it when its key or sigil is held, else shows the lock message.
pub fn vs_field_interactWithLockedRoomObject(arg0: ?*v._mpdRoomSectionA) callconv(.c) c_int {
    const sec = arg0.?;
    if (sec.unkE != 0) return 1;
    if (sec.unk12 != 0) return 1;

    // sp10[1] and sp10[3] are never written in the original; they are zeroed here.
    var sp10 = [_]i16{ 0, 0, 0, 0 };
    sp10[0] = psx.conv(i16, (@as(c_int, sec.unk0.unk0) << 7) +% 0x40);
    sp10[2] = psx.conv(i16, (@as(c_int, sec.unk0.unk2) << 7) +% 0x40);
    var action: c_int = 0;

    switch (sec.unk0.unk8.lock.state) {
        0 => action = 1,
        1, 3 => {
            if (c.vs_battle_itemIdIsInInventory(@as(c_int, sec.keyOrSigil)) != 0) {
                if (@as(c_int, sec.keyOrSigil) >= c.chamomileSigil) {
                    c.vs_battle_setStateFlag(psx.conv(c_short, @as(c_int, sec.keyOrSigil) +% 0x1B6), 2);
                    _ = c.vs_battle_decreaseMiscCount(@as(c_int, sec.keyOrSigil));
                }
                sec.unk0.unk8.lock.state = 0;
                action = 2;
            } else if (c.vs_battle_getStateFlag(psx.conv(c_short, @as(c_int, sec.keyOrSigil) +% 0x1B6)) != 0) {
                sec.unk0.unk8.lock.state = 0;
                action = 3;
            }
        },
        else => {},
    }

    if (action != 0) {
        if (action == 2) {
            var template: c_int = 7;
            if (@as(c_int, sec.keyOrSigil) >= c.chamomileSigil) {
                template = 9;
            }
            c.vs_battle_displaySceneMessage(template, @as(c_int, sec.keyOrSigil), 1);
        }
        c.func_8009E070(0, &sp10, 5);
        _ = c.vs_field_beginDoorToggle(psx.cptr(c._mpdRoomSectionA, sec));
        return 0;
    }

    if (sec.unk0.unk8.lock.state != 0) {
        // Other lock states leave template unset in the original; it is zeroed here.
        var template: c_int = 0;
        switch (sec.unk0.unk8.lock.state) {
            1 => {
                template = 6;
                if (@as(c_int, sec.keyOrSigil) >= c.chamomileSigil) {
                    template = 8;
                }
            },
            2 => template = 5,
            3 => template = 10,
            else => {},
        }
        c.vs_battle_displaySceneMessage(template, @as(c_int, sec.keyOrSigil), 1);
    }

    c.vs_main_playSfxDefault(0x7E, 0x65);
    c.vs_main_playSfxDefault(0x7E, 0x66);
    c.func_8009E5C4(0);
    return 0;
}
