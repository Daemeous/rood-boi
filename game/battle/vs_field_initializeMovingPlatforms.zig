//! Resets the room's section-12 counters and writes the per-entry data of the
//! room's section 12 into the function table (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_initializeMovingPlatforms (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Clears the section-12 state, then applies each section-12 entry to its table slot and the room's counter-driven call.
pub fn vs_field_initializeMovingPlatforms() callconv(.c) void {
    v.g.vs_field_areMovingPlatformsPaused().* = 0;
    v.g.vs_field_platformFreezeFrames().* = 0;
    v.g.vs_field_isPlatformFreezeTintActive().* = 0;

    {
        var k: usize = 0;
        while (k < 4) : (k += 1) {
            v.g.vs_field_platformBounceFrames()[k] = 7;
        }
    }

    if (psx.get(v.g.vs_battle_roomData().section12)) |first| {
        const temp_s3: c_int = psx.conv(c_int, v.g.vs_battle_roomData().header.section12Len / 184);
        var s2 = first;
        var i: c_int = 0;
        while (i < temp_s3) : ({
            i +%= 1;
            s2 = psx.at(s2, 1);
        }) {
            const temp_v0: [*c]c_int = c.vs_field_findRoomGeometryGroup(@as(c_int, psx.getBits(s2, "unk0_0")));
            if (temp_v0 != null) {
                if (psx.getBits(s2, "unk0_9") != 0) {
                    temp_v0[0] |= 0x100;
                }

                // ONE is 4096.
                temp_v0[1] = @divTrunc(s2.unkA8, 4096);
                temp_v0[2] = -%@divTrunc(s2.unkAC, 4096);
                temp_v0[3] = @divTrunc(s2.unkB0, 4096);
            }
        }
    }

    // vs_field_entranceDoorPlatformFlags is u16, promoted to int before the shift; the result is 0..7.
    const temp_a0: c_uint = psx.conv(c_uint, @as(c_int, v.g.vs_field_entranceDoorPlatformFlags().*) >> 13);
    if (temp_a0 >= 2) {
        c.vs_field_resetMovingPlatformWaypoint(psx.conv(c_int, temp_a0 -% 2), -1);
    }
}
