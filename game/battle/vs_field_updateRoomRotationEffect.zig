//! Steps one swinging rotation channel and writes its angle into the room geometry (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

/// orig: vs_field_updateRoomRotationEffect (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Steps a swinging angle channel, writes its angle into the room geometry, and returns 1 while it still swings.
pub fn vs_field_updateRoomRotationEffect(arg0: ?*v.vs_field_SwingEffectRecord) callconv(.c) c_int {
    const base: ?*i32 = psx.get(v.g.vs_battle_roomData().geometrySection);
    const s = arg0.?;
    var ret: c_int = 1;

    if (s.needsReset != 0) {
        s.needsReset = 0;
        s.angle = s.startAngle;
        s.angularVelocity = s.initialVelocity;
    }

    s.angle +%= s.angularVelocity;
    s.angularVelocity +%= s.acceleration *% 8;
    var delta: i32 = (s.angle -% s.restAngle) & 0xFFF;

    if (delta > 4096 / 2) {
        delta = 4096 -% delta;
    }

    const abs8: i32 = if (s.angularVelocity >= 0) s.angularVelocity else -%s.angularVelocity;
    if (delta < abs8) {
        // The shift amount is masked to 0..31, as the MIPS and x86 shifts do.
        const sh: u5 = @truncate(@as(u32, @bitCast(s.dampingShift)));
        s.angularVelocity = -%(s.angularVelocity >> sh);

        const abs8b: i32 = if (s.angularVelocity >= 0) s.angularVelocity else -%s.angularVelocity;
        if (abs8b < 0x21) {
            s.angle = s.restAngle;
            ret = 0;
        }
    }

    const off: isize = s.geometryOffset;
    if (s.axis == 1) {
        psx.byteAt(i32, base.?, off + 20).* = s.angle;
        psx.byteAt(i32, base.?, off).* |= 0x6000;
    } else if (s.axis == 2) {
        psx.byteAt(i32, base.?, off + 16).* = s.angle;
    } else {
        psx.byteAt(i32, base.?, off + 24).* = s.angle;
    }

    if (ret != 0) {
        return 1;
    }

    delta = s.startAngle;
    s.initialVelocity = -%s.initialVelocity;
    s.angularVelocity = s.initialVelocity;
    s.startAngle = s.restAngle;
    s.restAngle = delta;
    s.acceleration = -%s.acceleration;
    return 0;
}
