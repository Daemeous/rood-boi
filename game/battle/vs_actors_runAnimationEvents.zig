//! The actor animation script interpreter: runs the timed commands of an
//! actor's animation script (sounds, effects, weapon trails, vertex nudges,
//! movement targets) up to its current frame (pc/src/game/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// How an executed command continues (the C's labels).
const Tail = enum { next, reload, done };

fn word5AC(actor: *v.D_800F4538_t) c_int {
    return psx.bitsAt(c_int, &actor._bits_unk5AC_0, 0).*;
}

/// The vertex word `which` of vertex `index` in the actor's model (`(short*)(vertexOffset + index * 8)`).
fn vertexWord(actor: *v.D_800F4538_t, index: c_int, which: c_int) *align(1) i16 {
    const model = psx.get(actor.unk0.model).?;
    const base: [*]u8 = @ptrFromInt(@intFromPtr(model) +% model.vertexOffset);
    return psx.byteAt(i16, base, @as(isize, index) * 8 + @as(isize, which) * 2);
}

/// The sound pan of `target` (cases 5, 7, 8): 0x80 when it is a spectator, else from its position.
fn targetPan(id: c_int, target: *v.D_800F4538_t) c_int {
    if (psx.bitsAt(c_int, &target.unk0._bits_skip, 0).* & 0x3F9F00 != 0x8000) {
        _ = c.vs_actors_targetPoint(id, 0xFF, @ptrCast(&target.unk6FC), 0);
    }
    return c.vs_main_computeSfxPan(psx.fieldAt(c_int, &target.unk6FC, 0).*, target.unk6FC.vz);
}

/// `delta << 8 | lo` minus `base`, as the motion fields take it.
fn target16(script: [*]const u8, at: usize, base: c_int) i16 {
    const delta: c_int = script[at];
    return psx.conv(i16, ((delta << 8) | @as(c_int, script[at + 1])) -% base);
}

/// The heading delta wrapped to a half turn.
fn wrapHalf(motion: *v.D_800F4538_unk1864) void {
    if (motion.unkA < -0x800) motion.unkA = psx.conv(i16, @as(c_int, motion.unkA) +% 0x1000);
    if (motion.unkA > 0x800) motion.unkA = psx.conv(i16, @as(c_int, motion.unkA) -% 0x1000);
}

fn applyTranslation(motion: *v.D_800F4538_unk1864, arg: c_int) void {
    if (arg == 0) {
        motion.radiusExpandRate = psx.conv(u16, @as(c_int, motion.radiusExpandRate) +% motion.radiusExpandDelta);
        motion.radiusShrinkRate = psx.conv(u16, @as(c_int, motion.radiusShrinkRate) +% motion.radiusShrinkDelta);
    }
    motion.unk5 = psx.conv(u8, arg);
}

/// One command (the C's second switch). `arg` is the C's function-wide `arg`.
fn execute(actor: *v.D_800F4538_t, state: *v.D_800F4538_unkC54, mode: c_int, id: c_int, op: c_int, script: *[*]u8, arg: *c_int) Tail {
    const s = script.*;
    const motion = &actor.unk1864;
    var sfx0: c_int = undefined;
    var sfx1: c_int = undefined;
    switch (op) {
        14 => c.func_8006C480(id, s[1]),
        15 => c.func_8006C4A4(id, s[1]),
        29 => {
            psx.setBits(actor, "unk5AC_21", 1);
            actor.unk1802[0] = 0;
            psx.setBits(actor, "unk5AC_22", 0);
            actor.unk1802[1] = 0x20;
        },
        44 => c.vs_actors_textureOverlayMove(id, s),
        28 => c.vs_field_applyGimmickInteractionCommand(@ptrCast(actor), s[1]),
        23, 24 => {
            arg.* = if (op == 23) @as(c_int, actor.animationId) - 1 else @as(c_int, actor.animationId) + 1;
            if (arg.* >= 100) arg.* -= 100;
            _ = c.vs_actors_playActorAnimation(id, arg.*, 0, 0);
            return .reload;
        },
        19, 20 => {
            arg.* = s[1];
            sfx0 = if (op == 19) 13 else 1;
            _ = c.vs_actors_playActorAnimation(id, sfx0, 0, arg.*);
            return .reload;
        },
        55 => {
            const wait = psx.bitsAt(u8, &state._bits_unk548_0, 1);
            if (wait.* == 0) {
                wait.* = s[1];
            } else {
                wait.* -%= 1;
                if (wait.* == 0) return .next;
            }
            state.frame -%= 1;
            return .done;
        },
        26, 27 => {
            arg.* = 0;
            sfx0 = s[1];
            while (arg.* < 20) : (arg.* += 1) {
                const weapon = v.g.vs_battle_wepModels()[@intCast(arg.*)] orelse continue;
                if (weapon.nBones != 0 and weapon.unkD != 0 and psx.getBits(weapon, "actorId") == id) {
                    if (op == 26) {
                        if (c.vs_event_eventPlaybackState() != 4 and psx.getBits(&actor.unk0, "weaponDrawn") == 0) {
                            return .next;
                        }
                        psx.setBits(weapon, "unk8_4", 1);
                        weapon.unk11 = 0;
                        weapon.unk12 = 0x40;
                    } else {
                        weapon.unk11 = 0x40;
                        weapon.unk12 = 0;
                    }
                    weapon.unk13 = psx.conv(u8, sfx0);
                }
            }
        },
        21 => {
            _ = c.vs_actors_playActorAnimation(id, s[1], 0, 0);
            psx.setBits(actor, "unk5AC_16", 0);
            return .reload;
        },
        22 => {
            arg.* = s[1];
            sfx0 = s[2];
            _ = c.vs_actors_playActorAnimation(id, arg.*, 0, sfx0);
            psx.setBits(actor, "unk5AC_16", 0);
            return .reload;
        },
        4 => {
            arg.* = s[1];
            if (arg.* == 0x58) {
                if (actor.unk5B1 == 0 or psx.getBits(&actor.unk0, "weaponDrawn") == 0) {
                    arg.* = v.g.D_800E92DC()[0];
                } else {
                    arg.* = v.g.D_800E92DC()[actor.unk5B1];
                }
            }
            if (actor.unk0.position.vy < 3000) {
                c.vs_audio_playActorSoundPair(@ptrCast(actor), arg.*, 0);
            }
        },
        5, 8 => {
            arg.* = s[1];
            const target = psx.at(v.g.vs_actors_actorRecords(), id).*.?;
            sfx1 = s[2];
            if (target.unk17E4.unk0 == 2) {
                sfx0 = 0x80;
            } else {
                sfx0 = targetPan(id, target) >> 16;
                if (target.unk17E4.unk1 != 0) sfx0 = target.unk17E4.unk1;
            }
            if (sfx1 != 0) {
                c.vs_main_playSfx(if (op == 5) 0x7E else 0xFF000, arg.*, sfx0, sfx1);
            }
        },
        7 => {
            arg.* = s[1];
            const target = psx.at(v.g.vs_actors_actorRecords(), id).*.?;
            if (target.unk17E4.unk0 == 2) {
                sfx0 = 0x80;
                sfx1 = 0;
            } else {
                const value = targetPan(id, target);
                sfx0 = value >> 16;
                if (target.unk17E4.unk1 != 0) sfx0 = target.unk17E4.unk1;
                sfx1 = value & 0xFFFF;
                if (target.unk17E4.unk2 != 0) sfx1 = target.unk17E4.unk2;
            }
            if (sfx1 != 0) {
                c.vs_main_playSfx(0xFF000, arg.*, sfx0, sfx1);
            }
        },
        6 => c.vs_audio_stopSfx(0x7E, s[1]),
        9 => c.vs_audio_stopSfx(0xFF000, s[1]),
        11 => c.vs_audio_playActorSurfaceContactSound(id, @ptrCast(actor), op),
        1 => {
            c.vs_actors_initializeActorAnimationPose(actor, state);
            psx.setBits(actor, "unk5AC_16", 1);
            return .reload;
        },
        12 => {
            if (word5AC(actor) & 0x10000 == 0) _ = c.vs_effects_startEffectTrack(s[1]);
        },
        13 => {
            if (v.g.D_800F1868().* == 1 and word5AC(actor) & 0x40000 == 0) {
                c.vs_actors_initializeActorAnimationPose(actor, state);
                psx.setBits(actor, "unk5AC_16", 1);
                return .reload;
            }
            psx.setBits(actor, "unk5AC_18", 0);
        },
        2 => {
            if (c.func_8006C84C(id) == 0) {
                sfx0 = 1;
                _ = c.vs_actors_playActorAnimation(id, sfx0, 0, arg.*);
                return .reload;
            }
        },
        36, 37, 38 => {
            var delta: i16 = s[1];
            var packed_: i16 = psx.conv(i16, @as(c_int, delta) << 8);
            packed_ = psx.conv(i16, @as(c_int, packed_) | s[2]);
            arg.* = @as(c_int, packed_) & 0x1FF;
            packed_ >>= 9;
            delta = packed_;
            if (mode == 2) delta = psx.conv(i16, -%@as(c_int, packed_));
            const w = vertexWord(actor, arg.*, op - 36);
            w.* = psx.conv(i16, @as(c_int, w.*) +% delta);
            psx.setBits(actor, "unk5AC_23", 1);
        },
        39 => {
            const delta: i16 = s[1];
            var packed_: i16 = psx.conv(i16, @as(c_int, delta) << 8);
            packed_ = psx.conv(i16, @as(c_int, packed_) | s[2]);
            arg.* = @as(c_int, packed_) & 0x1FF;
            packed_ >>= 9;
            const dy: c_int = @as(i8, @bitCast(s[3]));
            const dz: c_int = @as(i8, @bitCast(s[4]));
            const w0 = vertexWord(actor, arg.*, 0);
            const w1 = vertexWord(actor, arg.*, 1);
            const w2 = vertexWord(actor, arg.*, 2);
            if (mode == 2) {
                w0.* = psx.conv(i16, @as(c_int, w0.*) -% packed_);
                w1.* = psx.conv(i16, @as(c_int, w1.*) -% dy);
                w2.* = psx.conv(i16, @as(c_int, w2.*) -% dz);
            } else {
                w0.* = psx.conv(i16, @as(c_int, w0.*) +% packed_);
                w1.* = psx.conv(i16, @as(c_int, w1.*) +% dy);
                w2.* = psx.conv(i16, @as(c_int, w2.*) +% dz);
            }
            psx.setBits(actor, "unk5AC_23", 1);
        },
        30, 31 => {
            arg.* = op - 30;
            const i: usize = @intCast(arg.* * 4); // 0 or 4: inside unk1802[10]
            actor.unk1802[i + 2] = s[1];
            actor.unk1802[i + 3] = s[2];
            actor.unk1802[i + 4] = s[3];
            actor.unk1802[i + 5] = s[4];
        },
        32 => _ = c.vs_actors_setTextureAnimation(id, 1, 1, 1),
        33 => _ = c.vs_actors_setTextureAnimation(id, 1, 1, 0),
        34 => _ = c.vs_actors_setTextureAnimation(id, 1, 0, 1),
        35 => _ = c.vs_actors_setTextureAnimation(id, 1, 0, 0),
        // Unprototyped in the unit: the C passes the script pointer.
        40 => @as(*const fn ([*]u8) callconv(.c) void, @ptrCast(&c.func_800AE46C))(s),
        41 => {
            @as(*const fn ([*]u8) callconv(.c) void, @ptrCast(&c.func_800AE474))(s);
            script.* = s + 5;
        },
        10 => c.vs_audio_playActorModelSound(@ptrCast(&actor.unk0), s[1]),
        25 => psx.setBits(actor, "unk5AC_20", 1),
        51 => {
            motion.radiusExpandDelta = target16(s, 1, motion.radiusExpandRate);
            arg.* = s[3];
            applyTranslation(motion, arg.*);
        },
        52 => {
            motion.radiusShrinkDelta = target16(s, 1, motion.radiusShrinkRate);
            arg.* = s[3];
            applyTranslation(motion, arg.*);
        },
        53 => {
            motion.radiusExpandDelta = target16(s, 1, motion.radiusExpandRate);
            motion.radiusShrinkDelta = target16(s, 3, motion.radiusShrinkRate);
            arg.* = s[5];
            applyTranslation(motion, arg.*);
        },
        54 => {
            motion.unkA = target16(s, 1, motion.unk8);
            wrapHalf(motion);
            arg.* = s[3];
            if (arg.* == 0) {
                motion.unk8 = psx.conv(u16, (@as(c_int, motion.unk8) +% motion.unkA) & 0xFFF);
            }
            motion.unk4 = psx.conv(u8, arg.*);
        },
        56, 57 => c.vs_actors_setActorBonePoseLock(@ptrCast(actor), s[1], op - 56),
        58 => {
            psx.setBits(&actor.unk0, "unk9_0", 2);
            if (actor.companionSlot >= 2) {
                psx.setBits(&psx.at(v.g.vs_actors_actorRecords(), actor.companionSlot).*.?.unk0, "unk9_0", 2);
            }
        },
        59 => {
            actor.unk0.unk34.vy = 3;
            psx.setBits(&actor.unk0, "unkA_0", 3);
            psx.setBits(&actor.unk0, "unkA_3", 0);
            psx.setBits(&actor.unk0, "unk9_6", 0);
        },
        61 => psx.setBits(actor, "unk5B0_3", 0),
        62 => psx.setBits(actor, "unk5B0_3", 1),
        60 => {
            arg.* = s[1];
            const point: [*]const u16 = @as([*]const u16, @ptrCast(&v.g.D_800E9A28()[0])) + @as(usize, @intCast(arg.*)) * 4;
            motion.radiusExpandDelta = psx.conv(i16, @as(c_int, point[0]) -% motion.radiusExpandRate);
            motion.radiusShrinkDelta = psx.conv(i16, @as(c_int, point[1]) -% motion.radiusShrinkRate);
            if (motion.unk5 == 0) {
                motion.radiusExpandRate = psx.conv(u16, @as(c_int, motion.radiusExpandRate) +% motion.radiusExpandDelta);
                motion.radiusShrinkRate = psx.conv(u16, @as(c_int, motion.radiusShrinkRate) +% motion.radiusShrinkDelta);
            }
            const delta: i16 = @bitCast(point[2]);
            motion.unkA = psx.conv(i16, @as(c_int, delta) -% motion.unk8);
            wrapHalf(motion);
            if (motion.unk4 == 0) {
                motion.unk8 = psx.conv(u16, (@as(c_int, motion.unk8) +% motion.unkA) & 0xFFF);
            }
        },
        63 => {
            if (word5AC(actor) & 0x10000 == 0) c.func_800CF8BC();
        },
        64 => psx.setBits(actor, "unk5AC_20", 0),
        else => {
            state.unk542 = 0xFFFF;
            return .done;
        },
    }
    return .next;
}

/// orig: vs_actors_runAnimationEvents (pc/src/game/BATTLE/BATTLE.PRG/44F14.c)
/// Runs `actor`'s animation script commands due by its frame counter; `mode` 1 and 2 run only one class of command, 3 looks for a wait.
pub fn vs_actors_runAnimationEvents(actor: ?*v.D_800F4538_t, state: ?*v.D_800F4538_unkC54, mode: c_int) callconv(.c) c_int {
    const a = actor.?;
    const st = state.?;
    const id: c_int = a.unk0.slot;
    var script: [*]u8 = @ptrCast(psx.get(st.unk544).?);
    // Uninitialised in the original until the first command sets it; read by
    // command 2's animation call when it comes first (0 here).
    var arg: c_int = 0;
    while (true) {
        const op: c_int = script[0];
        const run: bool = switch (mode) {
            0 => true,
            1 => op == 0x2C or op == 0x19 or @as(c_uint, @bitCast(op -% 0x1D)) < 0x12 or
                @as(c_uint, @bitCast(op -% 0x10)) < 3 or @as(c_uint, @bitCast(op -% 0x33)) < 4 or
                @as(c_uint, @bitCast(op -% 0x38)) < 2 or @as(c_uint, @bitCast(op -% 0x20)) < 8,
            2 => @as(c_uint, @bitCast(op -% 0x24)) < 4,
            3 => blk: {
                if (op != 2) break :blk false;
                return st.unk542;
            },
            else => true,
        };
        const tail: Tail = if (run) execute(a, st, mode, id, op, &script, &arg) else .next;
        switch (tail) {
            .done => return 0,
            .next => {
                script += v.g.vs_actors_animationEventLengths()[@intCast(op)];
                arg = script[0];
                if (arg == 0xFF) {
                    arg = @as(c_int, script[1]) + 0xFF;
                    script += 2;
                } else {
                    script += 1;
                }
                st.unk542 = psx.conv(u16, arg);
                psx.put(&st.unk544, &script[0]);
            },
            .reload => {
                arg = st.unk542;
                script = @ptrCast(psx.get(st.unk544).?);
                st.frame = 0;
            },
        }
        const counter: c_int = if (mode >= 2) @as(u16, @bitCast(a.unk5BC)) else @as(i16, @bitCast(st.frame));
        if (!(counter < arg)) continue;
        return 0;
    }
}
