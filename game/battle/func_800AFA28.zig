//! Per-frame animation update for a battle actor: advances the shadow tween and
//! the animation frame, applies the frame's root motion and bone poses
//! (pc/src/game/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

// Callees the unit's translate-c declarations do not list: linked by their
// original symbols.
const LoadAnimFn = *const fn (*anyopaque, c_int, *anyopaque) callconv(.c) c_int;
/// (A function, not a module-level pointer constant: that would be a relocated data slot in the engine library.)
inline fn loadAnimation() LoadAnimFn {
    return @extern(LoadAnimFn, .{ .name = "vs_actors_findActorAnimationHeader" });
}

const QueueAnimFn = *const fn (c_int, c_int, c_int, c_int) callconv(.c) void;
/// (A function, not a module-level pointer constant: that would be a relocated data slot in the engine library.)
inline fn queueAnimation() QueueAnimFn {
    return @extern(QueueAnimFn, .{ .name = "vs_actors_playActorAnimationAtFrame" });
}

const EventFn = *const fn (*anyopaque, *anyopaque, c_int) callconv(.c) void;
/// (A function, not a module-level pointer constant: that would be a relocated data slot in the engine library.)
inline fn frameEvent() EventFn {
    return @extern(EventFn, .{ .name = "vs_actors_runAnimationEvents" });
}

/// A bone pose inside the state block at byte offset `off` (always even), as a C pointer.
fn poseAt(st: *v.D_800F4538_unkC54, off: c_int) [*c]c.SVECTOR {
    const p: *v.SVECTOR = @alignCast(psx.byteAt(v.SVECTOR, st, @as(isize, off)));
    return psx.cptr(c.SVECTOR, p);
}

/// orig: func_800AFA28 (pc/src/game/BATTLE/BATTLE.PRG/44F14.c)
/// Advances an actor's animation by its pending frames and applies the frame's root motion and bone poses.
pub fn func_800AFA28(actor: ?*v.D_800F4538_t, state: ?*v.D_800F4538_unkC54, mode: c_int) callconv(.c) void {
    const a = actor.?;
    const st = state.?;
    const scratch: *c.animationFrameScratch = pad.at(c.animationFrameScratch, 0x3EC);

    // The state word at unk548_0: bit 17 cleared here, set and cleared below.
    const flags = psx.bitsAt(c_uint, &st._bits_unk548_0, 0);
    flags.* &= 0xFFFD_FFFF;

    var frameOrOffset: c_int = @as(c_int, psx.conv(i16, st.unk540));
    var i: c_int = 0;
    if (a.unk5B2 != 0) {
        const shadow = &a.unk1864;
        while (true) {
            if (shadow.unk4 != 0) {
                const delta = @divTrunc(@as(c_int, shadow.unkA), @as(c_int, shadow.unk4));
                shadow.unkA = psx.conv(i16, @as(c_int, shadow.unkA) -% delta);
                shadow.unk8 = psx.conv(u16, @as(c_int, shadow.unk8) +% delta);
                shadow.unk4 -%= 1;
            }
            shadow.unk8 &= 0xFFF;
            if (shadow.unk5 != 0) {
                var delta = @divTrunc(@as(c_int, shadow.radiusExpandDelta), @as(c_int, shadow.unk5));
                shadow.radiusExpandDelta = psx.conv(i16, @as(c_int, shadow.radiusExpandDelta) -% delta);
                shadow.radiusExpandRate = psx.conv(u16, @as(c_int, shadow.radiusExpandRate) +% delta);
                delta = @divTrunc(@as(c_int, shadow.radiusShrinkDelta), @as(c_int, shadow.unk5));
                shadow.radiusShrinkDelta = psx.conv(i16, @as(c_int, shadow.radiusShrinkDelta) -% delta);
                shadow.radiusShrinkRate = psx.conv(u16, @as(c_int, shadow.radiusShrinkRate) +% delta);
                shadow.unk5 -%= 1;
            }
            if (@as(c_int, psx.conv(i16, st.unk540)) >= @as(c_int, psx.conv(u16, a.unk5BC))) {
                flags.* |= 0x20000;
                const queued = a.unk6E0;
                if (queued != 0) {
                    queueAnimation()(@as(c_int, a.unk0.slot), @as(c_int, a.unk6E0), @as(c_int, a.unk6E1), @as(c_int, a.unk6E2));
                    a.unk6E0 = 0;
                    return;
                }
            }
            if ((psx.bitsAt(c_uint, &a.unk0._bits_skip, 0).* & 4) != 0) {
                if (@as(c_int, st.unk542) == @as(c_int, psx.conv(i16, st.unk540))) {
                    frameEvent()(@ptrCast(a), @ptrCast(st), mode);
                }
            }
            if (@as(c_int, psx.conv(i16, st.unk540)) >= @as(c_int, psx.conv(u16, a.unk5BC))) {
                st.unk540 -%= 1;
                flags.* |= 0x10000;
            }
            st.unk540 +%= 1;
            i +%= 1;
            if (i >= @as(c_int, a.unk5B2)) break;
        }
    }

    // update_bones:
    const frame: c_int = @as(c_int, psx.conv(i16, st.unk540));
    if (frame != 1 and (flags.* & 0x10000) == 0) {
        const lastFrame = frame -% 1;
        if (psx.bitsAt(u8, &st._bits_unk548_0, 1).* == 0) {
            if (lastFrame < frameOrOffset) frameOrOffset = 1;
            if (frameOrOffset == 0) frameOrOffset = 1;

            var header: [*c]u16 = null;
            const dataWord: c_uint = @bitCast(loadAnimation()(@ptrCast(a), @as(c_int, a.animationId), @ptrCast(&header)));
            if (dataWord != 0) {
                const frames: *align(1) u8 = @ptrFromInt(@as(usize, dataWord));
                scratch.first = psx.conv(@TypeOf(scratch.first), frameOrOffset);
                scratch.last = psx.conv(@TypeOf(scratch.last), lastFrame);
                psx.put(&scratch.data, frames);

                const rootTrack: u16 = psx.at(header, 4).*;
                if (rootTrack != 0) {
                    c.vs_actors_decodeAnimationTrack(@as(c_int, rootTrack), psx.cptr(c.SVECTOR, &st.unk150[41]), 0);

                    var x: c_int = @as(c_int, st.unk150[41].vx);
                    if (x < 0) x +%= 3;
                    a.unk0.position.vx = psx.conv(i16, @as(c_int, psx.conv(u16, a.unk0.position.vx)) +% (x >> 2));

                    var y: c_int = @as(c_int, st.unk150[41].vy);
                    if (y < 0) y +%= 3;
                    a.unk0.position.vy = psx.conv(i16, @as(c_int, psx.conv(u16, a.unk0.position.vy)) +% (y >> 2));

                    var z: c_int = @as(c_int, st.unk150[41].vz);
                    if (z < 0) z +%= 3;
                    a.unk0.position.vz = psx.conv(i16, @as(c_int, psx.conv(u16, a.unk0.position.vz)) +% (z >> 2));
                }

                c.vs_actors_decodeAnimationTrack(@as(c_int, psx.at(header, 3).*), psx.cptr(c.SVECTOR, &st.unk0[41]), 0);

                i = 0;
                if (a.unk0.nBones != 0) {
                    frameOrOffset = 0x150;
                    // boneState = state + boneOff: each bone's pose is 8 bytes further on.
                    var boneOff: c_int = 0;
                    while (true) {
                        c.vs_actors_decodeAnimationTrack(
                            @as(c_int, psx.at(header, i +% 5).*),
                            poseAt(st, boneOff),
                            1,
                        );
                        if ((psx.byteAt(u8, header, 3).* & 2) != 0) {
                            c.vs_actors_decodeAnimationTrack(
                                @as(c_int, psx.at(header, i +% @as(c_int, a.unk0.nBones) +% 5).*),
                                poseAt(st, frameOrOffset),
                                0,
                            );
                        }
                        frameOrOffset +%= 8;
                        boneOff +%= 8;
                        i +%= 1;
                        if (i >= @as(c_int, a.unk0.nBones)) break;
                    }
                }
            }
        }
    }
}
