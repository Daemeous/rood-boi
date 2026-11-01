//! An actor's per-frame transform update: eases its scale, rotation and
//! position deltas, places it, and turns its head toward a target
//! (src/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

fn word8(a: *v.D_800F4538_t) c_int {
    return psx.byteAt(c_int, a, 8).*;
}

/// The C's `apply:` tail: the head angles clamped to the actor's limit around the current ones.
fn apply(a: *v.D_800F4538_t, scratch: *v.actorTransformScratch, yaw0: c_int, pitch0: c_int) void {
    var yaw = yaw0;
    var pitch = pitch0;
    const limit: c_int = a.unk17F4.pad;
    yaw -%= @as(i16, @bitCast(a.unk0.unk14));
    if (yaw > limit) yaw = limit;
    if (yaw < -%limit) yaw = -%limit;
    pitch -%= @as(i16, @bitCast(a.unk0.unk16));
    if (pitch > limit) pitch = limit;
    if (pitch < -%limit) pitch = -%limit;
    scratch.angleX = psx.conv(u16, @as(c_int, a.unk0.unk14) +% yaw);
    scratch.angleY = psx.conv(u16, @as(c_int, a.unk0.unk16) +% pitch);
    a.unk0.unk14 = scratch.angleX;
    a.unk0.unk16 = scratch.angleY;
}

fn wrapClamp(angle0: c_int) c_int {
    var angle = angle0;
    if (angle >= 0x800) angle -%= 0x1000;
    if (angle < -0x800) angle +%= 0x1000;
    if (angle > 800) angle = 800;
    if (angle < -800) angle = -800;
    return angle;
}

/// orig: func_800B002C (src/BATTLE/BATTLE.PRG/44F14.c)
/// Steps `actor`'s eased scale, rotation and movement (unless `arg1`), transforms it, and aims its head (scratch angles) at its look target.
pub fn func_800B002C(actor: ?*v.D_800F4538_t, arg1: c_int) callconv(.c) void {
    const scratch: *v.actorTransformScratch = @ptrFromInt(0x1F80035C);
    const a = actor.?;
    const body = &a.unk0;

    if (psx.byteAt(c_int, &body.unk2C, 0).* == 0x10001000 and body.unk30 == 4096) {
        scratch.offset.vx = @as(i16, @bitCast(psx.byteAt(u16, a, 0x84C).*)) >> 1;
        scratch.offset.vy = @as(i16, @bitCast(psx.byteAt(u16, a, 0x84E).*)) >> 1;
        scratch.offset.vz = @as(i16, @bitCast(psx.byteAt(u16, a, 0x850).*)) >> 1;
    } else {
        scratch.offset.vx = psx.conv(i16, @divTrunc(@as(c_int, psx.byteAt(i16, a, 0x84C).*) *% body.unk2E, 8192));
        scratch.offset.vy = psx.conv(i16, @divTrunc(@as(c_int, psx.byteAt(i16, a, 0x84E).*) *% body.unk30, 8192));
        scratch.offset.vz = psx.conv(i16, @divTrunc(@as(c_int, psx.byteAt(i16, a, 0x850).*) *% body.unk2C, 8192));
    }
    scratch.offset.vx = psx.conv(i16, @divTrunc(@as(c_int, scratch.offset.vx) *% a.unk183C, 4096));
    scratch.offset.vy = psx.conv(i16, @divTrunc(@as(c_int, scratch.offset.vy) *% a.unk183C, 4096));
    scratch.offset.vz = psx.conv(i16, @divTrunc(@as(c_int, scratch.offset.vz) *% a.unk183C, 4096));

    if (arg1 == 0) {
        if (a.unk5C8 != 0) {
            const n: c_int = a.unk5C8;
            var dx: c_int = @divTrunc(@as(c_int, body.unk44.vx), n);
            body.unk44.vx = psx.conv(i16, @as(c_int, body.unk44.vx) -% dx);
            body.unk2C = psx.conv(i16, @as(c_int, body.unk2C) +% dx);
            dx = @divTrunc(@as(c_int, body.unk44.vy), a.unk5C8);
            body.unk44.vy = psx.conv(i16, @as(c_int, body.unk44.vy) -% dx);
            body.unk2E = psx.conv(i16, @as(c_int, body.unk2E) +% dx);
            dx = @divTrunc(@as(c_int, body.unk44.vz), a.unk5C8);
            body.unk44.vz = psx.conv(i16, @as(c_int, body.unk44.vz) -% dx);
            body.unk30 = psx.conv(i16, @as(c_int, body.unk30) +% dx);
            a.unk5C8 -%= 1;
        }
        if (body.unk18 != 0) {
            var dx: c_int = @divTrunc(@as(c_int, body.unk3C), body.unk18);
            body.unk3C = psx.conv(i16, @as(c_int, body.unk3C) -% dx);
            body.pitch = psx.conv(i16, (@as(c_int, body.pitch) +% dx) & 0xFFF);
            dx = @divTrunc(@as(c_int, body.unk3E), body.unk18);
            body.unk3E = psx.conv(i16, @as(c_int, body.unk3E) -% dx);
            body.facing = psx.conv(i16, (@as(c_int, body.facing) +% dx) & 0xFFF);
            dx = @divTrunc(@as(c_int, body.unk40), body.unk18);
            body.unk40 = psx.conv(i16, @as(c_int, body.unk40) -% dx);
            body.roll = psx.conv(i16, (@as(c_int, body.roll) +% dx) & 0xFFF);
            body.unk18 -%= 1;
        }
        if (psx.getBits(a, "unk5B0_4") != 0) {
            a.unk704.unk0[0].vx = 0x800;
            a.unkC54.unk0[0].vx = 0x800;
        }
        if (body.unk1A != 0) {
            var dx: c_int = @divTrunc(@as(c_int, body.unk34.vx), body.unk1A);
            body.unk34.vx = psx.conv(i16, @as(c_int, body.unk34.vx) -% dx);
            body.position.vx = psx.conv(i16, @as(c_int, body.position.vx) +% dx);
            dx = @divTrunc(@as(c_int, body.unk34.vy), body.unk1A);
            body.unk34.vy = psx.conv(i16, @as(c_int, body.unk34.vy) -% dx);
            body.position.vy = psx.conv(i16, @as(c_int, body.position.vy) +% dx);
            dx = @divTrunc(@as(c_int, body.unk34.vz), body.unk1A);
            body.unk34.vz = psx.conv(i16, @as(c_int, body.unk34.vz) -% dx);
            body.position.vz = psx.conv(i16, @as(c_int, body.position.vz) +% dx);
            body.unk1A -%= 1;
            if (body.unk1A == 0) {
                if (psx.getBits(body, "unkA_3") == 1) {
                    psx.setBits(body, "unkA_3", 0);
                    psx.setBits(body, "unk9_6", 0);
                    a.unk6E3 = 0xFF;
                }
                if (psx.getBits(body, "unkA_3") != 2) {
                    body.currentTileX = psx.conv(u8, @divTrunc(@as(c_int, body.position.vx), 128));
                    body.currentTileZ = psx.conv(u8, @divTrunc(@as(c_int, body.position.vz), 128));
                    _ = c.func_800A92B8(body.currentTileX, body.currentTileZ);
                    psx.setBits(a, "unk5AC_28", psx.conv(u4, psx.global(u8, "D_800F49F8").*));
                }
            }
        }
    }
    c.func_800B07DC(@ptrCast(a));
    if (word8(a) & 0x3F0000 == 0) {
        psx.byteAt(c_int, &body.lastTouchedTileX, 0).* = psx.byteAt(c_int, &body.currentTileX, 0).*;
        psx.byteAt([2]c_int, &body.unk4C, 0).* = psx.byteAt([2]c_int, &body.position, 0).*;
        if (psx.getBits(body, "unkC_0") == 0) {
            a.unk1868 = psx.byteAt(c_int, &body.currentTileX, 0).*;
        }
        body.unk52 = body.facing;
    }

    if (a.unk17FC != -2) {
        track: {
            if (c.func_800BEB00() != 4) {
                var trackAnim = false;
                if (word8(a) < 0) {
                    return;
                }
                var dx: c_int = a.animationId;
                if (dx >= 100) {
                    dx -= 100;
                }
                if (word8(a) & 0x400000 != 0) {
                    if (@as(c_uint, @bitCast(dx -% 31)) < 8) trackAnim = true;
                } else if (word8(a) & 0x200000 != 0) {
                    if (@as(c_uint, @bitCast(dx -% 8)) < 27) trackAnim = true;
                } else if (dx == 0) {
                    break :track;
                } else if (dx == 1 or dx == 6 or dx == 13 or dx == 14 or dx == 17 or dx == 18) {
                    break :track;
                }
                if (!trackAnim) {
                    return apply(a, scratch, 0, 0);
                }
            }
        }
        var yaw: c_int = undefined;
        var pitch: c_int = undefined;
        if (a.unk17FC == -4) {
            pitch = a.unk17F4.vx;
            yaw = a.unk17F4.vy;
        } else if (a.unk17FC == -3) {
            pitch = @as(c_int, a.unk17F4.vx) - body.pitch;
            yaw = @as(c_int, a.unk17F4.vy) - body.facing;
        } else {
            var dx: c_int = undefined;
            var dz: c_int = undefined;
            if (a.unk17FC == -1) {
                dx = @as(c_int, a.unk17F4.vx) - body.position.vx;
                dz = @as(c_int, a.unk17F4.vz) - body.position.vz;
            } else {
                const other = psx.at(v.g.D_800F4538(), a.unk17FC).*.?;
                dx = @as(c_int, other.unk0.position.vx) - body.position.vx;
                dz = @as(c_int, other.unk0.position.vz) - body.position.vz;
            }
            yaw = 0xC00 -% c.ratan2(dz, dx);
            yaw &= 0xFFF;
            yaw -%= body.facing;
            dx *%= dx;
            if (dx < 0) dx = -%dx;
            dz *%= dz;
            if (dz < 0) dz = -%dz;
            dx = c.vs_gte_rsqrt(dx +% dz);
            if (a.unk17FC == -1) {
                dz = @as(c_int, a.unk17F4.vy) - body.position.vy;
            } else {
                dz = @as(c_int, psx.at(v.g.D_800F4538(), a.unk17FC).*.?.unk0.position.vy) - body.position.vy;
            }
            pitch = c.ratan2(dx, dz);
            pitch -%= 0x400;
            pitch &= 0xFFF;
        }
        yaw = wrapClamp(yaw);
        pitch = wrapClamp(pitch);
        apply(a, scratch, yaw, pitch);
    } else if (psx.byteAt(c_int, &body.unk14, 0).* != 0) {
        apply(a, scratch, 0, 0);
    } else {
        scratch.angleX = 0;
        scratch.angleY = 0;
    }
}
