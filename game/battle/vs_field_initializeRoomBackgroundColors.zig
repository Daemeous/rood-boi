//! Room section B setup: resets the room's origin and scale, then fixes up the
//! room's bounds from its collision map (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_initializeRoomBackgroundColors (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Resets the room origin and scale, and sizes the bounds of the room's section B.
pub fn vs_field_initializeRoomBackgroundColors() callconv(.c) void {
    v.g.vs_field_backgroundOffsetX().* = 0;
    v.g.vs_field_backgroundOffsetY().* = 0;
    v.g.vs_field_backgroundParallaxScaleXFixed12().* = 0x1000;
    v.g.vs_field_backgroundParallaxScaleYFixed12().* = 0x1000;

    if (psx.get(v.g.vs_battle_roomData().sectionB)) |temp| {
        var i: c_int = 1;
        while (i < 5) : (i +%= 1) {
            if (temp.unk0[0] != temp.unk0[@intCast(i)]) {
                break;
            }
        }
        if (i == 5 and (temp.unk0[0] & 0xFFFFFF) != 0) {
            v.g.vs_field_isSolidBackground().* = 1;
        } else {
            v.g.vs_field_isSolidBackground().* = 0;
            c.vs_field_setRoomBackgroundColors(@ptrCast(&temp.unk0));
        }

        if (temp.unk48[0].s32 == 0 and temp.unk48[1].s32 == 0) {
            if (psx.get(v.g.vs_battle_roomData().collisionMapSection)) |collisionMap| {
                temp.unk48[1].s16[0] = psx.conv(i16, @as(c_int, collisionMap.width) *% 4);
                temp.unk48[1].s16[1] = psx.conv(i16, @as(c_int, collisionMap.height) *% 4);
            } else {
                temp.unk48[1].s16[0] = 0x20;
                temp.unk48[1].s16[1] = 0x20;
            }
        }

        temp.unk48[0].s16[0] = psx.conv(i16, @as(c_int, temp.unk48[0].s16[0]) *% 128);
        temp.unk48[0].s16[1] = psx.conv(i16, @as(c_int, temp.unk48[0].s16[1]) *% 128);
        temp.unk48[1].s16[0] = psx.conv(i16, @as(c_int, temp.unk48[1].s16[0]) *% 128);
        temp.unk48[1].s16[1] = psx.conv(i16, @as(c_int, temp.unk48[1].s16[1]) *% 128);
        temp.unk48[1].s16[0] = psx.conv(i16, @as(c_int, temp.unk48[1].s16[0]) -% @as(c_int, temp.unk48[0].s16[0]));
        temp.unk48[1].s16[1] = psx.conv(i16, @as(c_int, temp.unk48[1].s16[1]) -% @as(c_int, temp.unk48[0].s16[1]));
    }
}
