//! An actor's airborne states: the jump's wind-up, the rise and fall with its
//! landing, bounce and slide limits, and the landing's recovery
//! (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

const Actor = v.D_800F4538_t;

fn halfTick() c_int {
    return @divTrunc(v.g.vs_gametime_tickspeed().*, 2);
}

/// `(signed char)actor->unk0.unk11`.
fn timer(actor: *Actor) i8 {
    return @bitCast(actor.unk0.unk11);
}

/// orig: func_800AC690_getProfile (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// The actor's sound profile: 3 (flags 0xF00), 4 (0x400000), 5 (0x200000),
/// 6 (0x5AC bit 3), else unk5B0's low two bits.
fn getProfile(actor: *Actor) c_int {
    const flags = common.object.flags8(actor).*;
    if (flags & 0xF00 != 0) return 3;
    if (flags & 0x400000 != 0) return 4;
    if (flags & 0x200000 != 0) return 5;
    if (common.object.flags5AC(actor).* & 8 != 0) return 6;
    return @as(c_int, psx.getBits(actor, "unk5B0_0")) & 3;
}

const Next = enum { proceed, land, fall };

/// orig: vs_actors_updateActorJumpAndFall (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Runs the actor's airborne state (flags bits 16..18): 2, the wind-up (unk11
/// frames, then the jump sound, or the take-off: vy -30, state 3, its
/// platform released); 3, the flight (gravity by state, the hover's
/// half-steps at 0x400000, a bounce off the floor or a slope, the wind
/// sound by speed, the slide limits unk1814/unk1818 and the platform jump of
/// unk181A 2) and the landing (on a platform, or the ground: contact, fall
/// damage sounds 0x41/0x42 by height, the landing sound, state 4); 4, the
/// recovery (unk11 frames, then the actor's end of the jump).
pub fn vs_actors_updateActorJumpAndFall(arg0: c_int, arg1: ?*Actor) callconv(.c) void {
    const actor = arg1.?;
    const profile = getProfile(actor);
    const flags = common.object.flags8(actor).*;
    const state: c_int = @intCast((flags >> 16) & 7);

    switch (state) {
        2 => {
            if (timer(actor) > 0) {
                const t: i8 = @truncate(@as(c_int, actor.unk0.unk11) -% halfTick());
                actor.unk0.unk11 = @bitCast(t);
                if (t < 2) {
                    c.vs_actors_playActorAnimation(arg0, psx.at(v.g.D_800E92E8(), profile).*, 0, 8);
                    actor.unk0.unk11 = 0;
                }
            } else {
                if (actor.unk181A == 0) actor.unk0.unk34.vy = -30;
                actor.unk5B7 = 0;
                common.object.flags8(actor).* = (common.object.flags8(actor).* & 0xFFF8FFFF) | 0x30000;
                if (common.object.flagsC(actor).* & 0xF != 0) {
                    c.vs_field_setMovingPlatformActor(@as(c_int, psx.getBits(&actor.unk0, "unkC_0")) -% 2, -1);
                    common.object.flagsC(actor).* &= ~@as(c_uint, 0xF);
                }
            }
        },
        3 => airborne(arg0, actor, profile, state),
        4 => {
            if (flags & 0x200000 != 0) {
                common.object.flags8(actor).* = flags & ~@as(c_uint, 0x200000);
                common.object.flags8(actor).* &= ~@as(c_uint, 0x400000);
            }
            c.vs_actors_saveActorGroundedTransform(@ptrCast(actor));
            actor.unk0.unk11 = psx.conv(u8, @as(c_int, actor.unk0.unk11) -% halfTick());
            if (timer(actor) < 0) actor.unk0.unk11 = 0;
            if (timer(actor) == 0) {
                actor.unk6EC = 0;
                if (actor.unk0.slot != 0) {
                    if (common.object.flags8(actor).* & 0x400000 == 0 and actor.unk5CC != 0) return;
                    if (profile != 3) c.vs_actors_playActorAnimation(arg0, 1, 0, 8);
                    c.vs_battle_aiReleaseAiDestinationTile(@as(c_int, actor.unk0.currentTileX), @as(c_int, actor.unk0.currentTileZ));
                    c.vs_battle_aiReleaseActorAiContactOccupancy(actor.unk0.slot);
                }
                actor.unk1846 = 0;
                common.object.flags8(actor).* &= 0xFFF8FFFF;
            }
        },
        else => {},
    }
}

/// The flight (state 3) and its landing or fall.
fn airborne(arg0: c_int, actor: *Actor, profile: c_int, state: c_int) void {
    const pos = &actor.unk0.position;
    const vel = &actor.unk0.unk34;
    if (timer(actor) > 0) {
        const t: i8 = @truncate(@as(c_int, actor.unk0.unk11) -% halfTick());
        actor.unk0.unk11 = @bitCast(t);
        if (t > 0) return;
        c.vs_actors_playActorAnimation(actor.unk0.slot, 0x2F, 0, 8);
        actor.unk0.unk11 = 0;
    }
    var y: c_int = undefined;
    var limit: c_int = undefined;
    if (common.object.flags8(actor).* & 0x400000 != 0) {
        // func_800AC690_timer_t.timer: the top byte of the word at 0x5B4 (unk5B7).
        y = halfTick();
        actor.unk5B7 = psx.conv(u8, @as(c_int, actor.unk5B7) +% y);
        limit = 7;
        y = actor.unk5B7 >> 1;
        actor.unk5B7 = psx.conv(u8, @as(c_int, actor.unk5B7) -% y *% 2);
    } else {
        limit = 9;
        y = halfTick() *% state;
    }
    const velocity: u16 = psx.conv(u16, @as(c_int, vel.vy) +% y);
    vel.vy = @bitCast(velocity);
    if (@as(i16, @bitCast(velocity)) < 0) {
        y = 1;
        var floor: c_int = c.vs_field_ceilingHeightAt(pos.vx, pos.vz);
        floor = @as(c_int, @bitCast(@as(c_uint, @bitCast(floor)) << 17)) >> 17;
        if (arg0 != 0) {
            y = 3;
            v.g.vs_field_platformContactSearchDepth().* = psx.conv(i16, @as(c_int, actor.menuCameraHeightOffset) +% 0x40);
        }
        const ground = c.vs_field_movingPlatformContactHeight(pos.vx, pos.vy, pos.vz, y);
        if (ground != 0 and floor < ground) floor = ground;
        const top = @as(c_int, pos.vy) +% vel.vy -% 0xC0;
        if (top < floor) {
            vel.vy = psx.conv(i16, -@as(c_int, vel.vy));
            if (arg0 != 0) {
                vel.vx = 0;
                vel.vz = 0;
                actor.unk181A = 0;
                const tile: *const [8]u8 = @ptrCast(&actor.unk5EC);
                c.vs_battle_aiReleaseAiDestinationTile(@as(c_int, tile[0]), @as(c_int, tile[2]));
            }
        }
    }
    y = vel.vy;
    if (y <= limit) {
        if (y >= -limit) c.vs_actors_playActorAnimation(arg0, psx.at(v.g.D_800E92F0(), profile).*, 0, 16);
    } else {
        c.vs_actors_playActorAnimation(arg0, psx.at(v.g.D_800E92F8(), profile).*, 0, 8);
    }

    var next: Next = .proceed;
    switch (actor.unk181A) {
        0 => if (vel.vy < 0) {
            next = .fall;
        },
        1 => slide(actor),
        2 => {
            const vy: c_int = vel.vy;
            actor.unk1816 -%= 1;
            if (vy < 0) {
                next = .fall;
            } else {
                y = c.vs_field_movingPlatformContactHeight(@as(c_int, pos.vx) +% vel.vx, @as(c_int, pos.vy) +% vy, @as(c_int, pos.vz) +% vel.vz, 2);
                const contact = v.g.D_800F49F4();
                const tile: *const [8]u8 = @ptrCast(&actor.unk5EC);
                if (contact.* != 0 and contact.* != tile[1]) {
                    actor.unk6E4 = psx.conv(u8, contact.*);
                    actor.unk0.currentTileX = psx.conv(u8, @divTrunc(@as(c_int, pos.vx), 128));
                    actor.unk0.currentTileZ = psx.conv(u8, @divTrunc(@as(c_int, pos.vz), 128));
                    actor.unk0.unk5D = 0;
                    c.vs_field_setMovingPlatformActor(contact.* -% 2, arg0);
                    c.vs_field_setMovingPlatformActor(contact.* -% 2, -1);
                    contact.* = 0;
                    c.func_800E6764(arg0);
                    next = .land;
                } else if (actor.unk1816 > 0) {
                    next = .fall;
                } else {
                    slide(actor);
                }
            }
        },
        else => {},
    }

    if (next == .proceed) {
        y = c.vs_field_sampleActorSupportHeight(@ptrCast(pos), vel.vx, vel.vz, 1);
        if (y == 3000 and psx.at(v.g.D_800E9278(), v.g.vs_field_collisionProbeTile().*.?[0] & 0x1F).* == 20) {
            if (pos.vy <= 0 and @as(c_int, pos.vy) +% vel.vy >= 0) c.func_8006F450(@ptrCast(pos));
        }
        const top = @as(c_int, pos.vy) +% vel.vy;
        next = .land;
        if (top < y) {
            next = .fall;
        } else if (v.g.D_800F49F4().* != 0 and y -% top < -0x80) {
            vel.vx = 0;
            vel.vz = 0;
            next = .fall;
        }
    }

    if (next == .fall) {
        c.vs_actors_setActorTranslation(arg0, @ptrCast(vel), 0);
        return;
    }

    // The landing.
    if (v.g.vs_actors_supportingGimmickIndex().* != 0) {
        const platform = psx.at(v.g.vs_field_gimmickInstances(), v.g.vs_actors_supportingGimmickIndex().*).*.?;
        if (platform.unk1A != 0) {
            pos.vy = psx.conv(i16, @as(c_int, platform.unk1E) -% 0x80);
            return;
        }
    }
    psx.fieldAt(i16, &actor.unk6EE, 0).* = 0;
    psx.setBits(&actor.unk0, "unkC_0", @truncate(@as(u8, @truncate(@as(c_uint, @bitCast(v.g.D_800F49F4().*))))));
    actor.unk0.unk5D = psx.conv(u8, v.g.D_800F49F4().*);
    psx.setBits(actor, "unk5AC_28", @truncate(v.g.vs_actors_supportingGimmickIndex().*));
    vel.vy = psx.conv(i16, y -% pos.vy);
    common.object.flags8(actor).* = (common.object.flags8(actor).* & ~@as(c_uint, 0x70000)) | 0x40000;
    c.vs_actors_setActorTranslation(arg0, @ptrCast(vel), 0);
    var floor: c_int = @as(c_int, pos.vy) -% psx.fieldAt(i16, &actor.unk0.unk4C, 2).*;
    floor = @divTrunc(floor, 64);
    actor.unk0.unk11 = 0;
    if (y < 3000) {
        const f = common.object.flags8(actor).*;
        if (actor.unk0.slot != 0 or f & 0xF00 == 0 or ((f >> 8) & 0xF) >= 6) {
            if (floor >= 4) {
                c.vs_audio_playActorSurfaceContactSound(arg0, @ptrCast(actor), 0x42);
                if (floor >= 6 and arg0 == 0) actor.unk0.unk11 = 8;
            } else {
                c.vs_audio_playActorSurfaceContactSound(arg0, @ptrCast(actor), 0x41);
            }
        }
    }
    actor.unk181A = 0;
    common.object.flags5AC(actor).* &= ~@as(c_uint, 0x0400_0000);
    c.vs_actors_playActorAnimation(arg0, psx.at(v.g.D_800E9300(), profile).*, 0, 8);
}

/// The slide limits: a move past unk1814 (x) or unk1818 (z) stops that axis.
fn slide(actor: *Actor) void {
    const pos = &actor.unk0.position;
    const vel = &actor.unk0.unk34;
    var speed = vel.vx;
    if (speed != 0) {
        const nextX = @as(c_int, pos.vx) +% speed;
        const hit = if (speed > 0) actor.unk1814 < nextX else nextX < actor.unk1814;
        if (hit) vel.vx = 0;
    }
    speed = vel.vz;
    if (speed != 0) {
        const nextZ = @as(c_int, pos.vz) +% speed;
        const hit = if (speed > 0) actor.unk1818 < nextZ else nextZ < actor.unk1818;
        if (hit) vel.vz = 0;
    }
}
