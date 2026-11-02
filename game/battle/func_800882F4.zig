//! The event timer D_80050468: counts up (mode 1) or down a tick (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_800882F4 (src/BATTLE/BATTLE.PRG/146C.c)
/// With the timer on: mode 1 counts up (stopping once hours turn positive, which then take the mode byte), other
/// modes count down to 0:00:00 (frames from 60); the frames (dithered by rand, times 5/3) and the other fields go
/// to state flags 0xA0..0xA3. Returns 1 when the frames are 0 and func_800BEC58(16) answers 1.
pub fn func_800882F4() callconv(.c) c_int {
    const timer = v.g.D_80050468();
    var result: c_int = 0;
    const waiting: c_int = 0; // var_s2: never set in the C
    if (@as(c_int, timer.unk0) == result) {
        return result;
    }
    if (timer.unk0 == 1) {
        if (timer.hours <= 0) {
            timer.frames = psx.conv(i8, @as(c_int, timer.frames) +% v.g.vs_gametime_tickspeed().*);
            if (timer.frames > 59) {
                timer.frames = 0;
                timer.seconds +%= 1;
                if (timer.seconds > 59) {
                    timer.seconds = 0;
                    timer.minutes +%= 1;
                    if (timer.minutes > 59) {
                        timer.minutes = 0;
                        timer.hours +%= 1;
                        if (timer.hours > 0) {
                            timer.hours = psx.conv(i8, timer.unk0);
                        }
                    }
                }
            }
        }
    } else if ((@as(c_int, timer.frames) | timer.seconds | timer.minutes | timer.hours) != 0) {
        if (timer.frames == 0) {
            timer.frames = 60;
            timer.seconds -%= 1;
            if (timer.seconds < 0) {
                timer.seconds = 59;
                timer.minutes -%= 1;
                if (timer.minutes < 0) {
                    timer.minutes = 59;
                    timer.hours -%= 1;
                    if (timer.hours < 0) {
                        timer.hours = 0;
                    }
                }
            }
        }
        timer.frames = psx.conv(i8, @as(c_int, timer.frames) -% v.g.vs_gametime_tickspeed().*);
    }
    if (timer.frames != 0) {
        const dithered = (psx.conv(c_uint, (@as(c_int, timer.frames) << 14) +% c.rand()) *% 5) >> 14;
        c.vs_battle_setStateFlag(0xA0, psx.conv(u8, dithered / 3));
    } else {
        c.vs_battle_setStateFlag(0xA0, psx.conv(u8, timer.frames));
    }
    c.vs_battle_setStateFlag(0xA1, psx.conv(u8, timer.seconds));
    c.vs_battle_setStateFlag(0xA2, psx.conv(u8, timer.minutes));
    c.vs_battle_setStateFlag(0xA3, psx.conv(u8, timer.hours));
    if (waiting == 0 and timer.frames == 0 and c.func_800BEC58(16, 0, 0, 0) == 1) {
        result = 1;
    }
    return result;
}
