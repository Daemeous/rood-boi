//! Tracking camera motion: orbits the camera around the battle centre and eases its facing toward the player (src/BATTLE/BATTLE.PRG/7EE98.c).
const psx = @import("common").psx;
const v = @import("views_7EE98");
const c = @import("c_7EE98");

fn abs32(x: c_int) c_int {
    return if (x >= 0) x else 0 -% x;
}

/// orig: func_800E7960 (src/BATTLE/BATTLE.PRG/7EE98.c)
/// Moves the tracking camera for one frame: its orbit radius, height, facing and target point.
pub fn func_800E7960(state: ?*v.trackingMotionState) callconv(.c) void {
    const st = state.?;
    const motion: *align(1) v.trackingMotion = @ptrCast(v.g.D_800F5920().*.?);
    const rate = v.g.D_800F5918();
    const drift = v.g.D_800F591C();

    if (motion.initialized == 0) {
        var initialAngle: c_int = c.ratan2(0x7C0 -% @as(c_int, st.x), 0x7C0 -% @as(c_int, st.z)) & 0xFFF;
        initialAngle = -%initialAngle;
        initialAngle +%= 0xC00;
        motion.facing = psx.conv(u16, initialAngle & 0xFFF);
        motion.height = st.y;
        motion.initialized = 1;
    }
    if (motion.flag3 == 0) {
        motion.radiusSquared = psx.conv(i32, c.func_800E4660(0x7C0 -% @as(c_int, st.x), 0, 0x7C0 -% @as(c_int, st.z)));
        motion.flag3 +%= 1;
    }
    psx.setBits(psx.get(st.context).?, "active", 0);

    var radius: c_int = c.vs_gte_rsqrt(motion.radiusSquared);
    if (radius < 1024) radius +%= 32;
    if (radius > 1024) radius -%= 32;
    var difference: c_int = radius -% 1024;
    if (difference < 0) difference = -%difference;
    if (difference < 32) radius = 1024;
    motion.radiusSquared = radius *% radius;

    const centerX: c_int = 0x7C0000;
    difference = @as(c_int, motion.height) -% 160;
    if (difference < 0) difference = -%difference;
    if (difference < 16) {
        motion.height = 160;
    } else if (motion.height < 160) {
        motion.height = psx.conv(i16, @as(c_int, motion.height) +% 12);
    } else {
        motion.height = psx.conv(i16, @as(c_int, motion.height) -% 12);
    }
    const centerY: c_int = @as(c_int, motion.height) << 12;
    const centerZ: c_int = 0x7C0000;

    const player: *align(1) v.trackingMotionActor = @ptrCast(v.g.D_800F4538()[0].?);
    const playerAngle = c.ratan2(0x7C0 -% @as(c_int, player.x), 0x7C0 -% @as(c_int, player.z));
    const currentAngle = c.ratan2(0x7C0 -% @as(c_int, st.x), 0x7C0 -% @as(c_int, st.z));

    const hit = c.func_800E78F4(@ptrCast(st)) != 0 and c.func_800D826C(@ptrCast(st)) != 0;
    const tracking = hit;
    const heading: c_int = if (hit) playerAngle else playerAngle +% 0x800;

    const sign: c_int = if (rate.* >= 0) 1 else -1;
    const cosCurrent = c.rcos(-%currentAngle +% 0xC00);
    const sinTarget = c.rsin(-%heading +% 0xC00);
    const sinCurrent = c.rsin(-%currentAngle +% 0xC00);
    const cosTarget = c.rcos(-%heading +% 0xC00);
    const aligned = ((cosCurrent *% sinTarget -% sinCurrent *% cosTarget) *% sign) >= 0;

    var delta: c_int = undefined;
    if (aligned) {
        if (abs32(rate.*) > 0x280000) {
            delta = @divTrunc(rate.*, 8) +% (sign << 16);
        } else {
            delta = sign << 16;
        }
    } else {
        delta = @divTrunc(rate.*, 4) +% (sign << 16);
    }
    if (tracking or c.rcos(currentAngle -% heading) < c.rcos(222)) {
        if (!aligned) delta = -%delta;
    }
    rate.* +%= delta;

    const newSign: c_int = if (rate.* >= 0) 1 else -1;
    if (abs32(rate.*) > 0x820000) rate.* = newSign *% 0x820000;

    const stats = psx.get(st.stats).?;
    const ratio = @divTrunc(@as(c_int, stats.current) << 16, @as(c_int, stats.maximum));

    if (motion.stateC == 0) {
        var trigger = false;
        if ((motion.phase == 0 and ratio < 0xC000) or (motion.phase == 1 and ratio < 0x4000)) {
            motion.phase +%= 1;
            trigger = true;
        } else if (abs32(rate.*) < 0x40000 and tracking and drift.* < 30 and (c.rand() & 511) == 0) {
            trigger = true;
        }
        if (trigger) motion.stateE = 1;
    }

    motion.facing = psx.conv(u16, @as(c_int, motion.facing) +% (rate.* >> 16));
    const facing: c_int = @as(c_int, motion.facing);
    st.targetActor = 0;
    const trigCos = c.rcos(facing);
    st.targetX = centerX +% radius *% trigCos;
    const trigSin = c.rsin(facing);
    st.targetZ = centerZ +% radius *% trigSin;

    var denominator: c_int = undefined;
    var scale: c_int = undefined;
    if (tracking) {
        if (aligned) {
            denominator = currentAngle -% playerAngle;
            scale = -80 -% drift.*;
        } else {
            const oppositeAngle = currentAngle -% 0x800;
            denominator = playerAngle -% oppositeAngle;
            scale = 300 -% drift.*;
        }
    } else if (aligned) {
        const oppositeAngle = currentAngle -% 0x800;
        denominator = playerAngle -% oppositeAngle;
        scale = 300 -% drift.*;
    } else {
        denominator = abs32(rate.*) >> 16;
        scale = 10;
    }
    denominator &= 0xFFF;
    if (denominator > 0x800) denominator = 0x1000 -% denominator;
    if (denominator == 0) {
        denominator = 1;
        scale = 0;
    }

    const magnitude = abs32(rate.*);
    var correction = @divTrunc(magnitude *% scale, denominator);
    if (abs32(correction) > magnitude *% 4) {
        correction = (correction >> 31) *% magnitude *% 4;
    }
    drift.* +%= correction >> 16;
    if (drift.* < -100) drift.* = -100;
    if (drift.* > 500) drift.* = 500;

    st.targetY = centerY -% (drift.* << 12);
    if (st.targetY >= 0xFF000) st.targetY = 0xFF000;
    c.func_800DEEFC(@ptrCast(st), 8);
}
