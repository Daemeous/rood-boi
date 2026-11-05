//! The battle charging skip, game side: the host code of pc/src/charge_skip.inc compiled into
//! src/BATTLE/BATTLE.PRG/146C.c. Its state stays in the unit's file statics (globalised, PORTING.md §6.5).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// D_800F19CC, read at each use as the C does.
fn block() *v.D_800F19CC_t {
    return v.g.D_800F19CC().*.?;
}

/// x86's float to int: out of range or NaN gives INT_MIN (the C's `(int)` of the setting).
fn floatToInt(x: f32) c_int {
    if (!(x > -2147483648.0 and x < 2147483648.0)) return -2147483648;
    return @intFromFloat(x);
}

/// orig: charge_skip_hold_vsyncs (src/BATTLE/BATTLE.PRG/146C.c)
/// The hold shown before Skip applies, in vsyncs: the setting's seconds for Ashley's (caster 0) or another's actions, at most 10.
pub fn holdVsyncs(caster: c_int) callconv(.c) c_int {
    var seconds: f32 = if (caster == 0) v.g.pc_qol_skip_charging_hold_mine().* else v.g.pc_qol_skip_charging_hold_enemy().*;
    if (!(seconds > 0.0)) return 0;
    if (seconds > 10.0) seconds = 10.0;
    return @intFromFloat(seconds * 60.0 + 0.5); // at most 600
}

/// orig: charge_skip_kind (src/BATTLE/BATTLE.PRG/146C.c)
/// The kind of action `index`: 1 a spell, 2 an enemy's art, 4 a Break Art, 8 an item.
pub fn kind(index: c_int) callconv(.c) c_int {
    return if (index < 0x8D) 1 else if (index < 0xB8) 2 else if (index < 0xE0) 4 else 8;
}

/// orig: pc_charge_skip_state (src/BATTLE/BATTLE.PRG/146C.c)
/// Whether this vsync is in an action's wind-up (camera mode 4..6) and whether Skip takes it out.
pub fn state(phase: *c_int, skipping: *c_int, actor: *c_int, action: *c_int) callconv(.c) c_int {
    if (v.g.D_800F196C().* != 2 or v.g.D_800F19CC().* == null or v.g._cameraMode().* < 4 or v.g._cameraMode().* > 6) {
        v.g.charge_skip_from_set().* = 0;
        return 0;
    }
    const a = &block().unk854[block().unk0 & 3];
    // the actions table has 256 entries: the index is checked first
    if (a.actionIndex >= 256 or psx.getBits(&v.g.actions()[a.actionIndex], "unk2_0") == 0) {
        v.g.charge_skip_from_set().* = 0;
        return 0;
    }
    const caster: c_int = a.unk4.unk0.targetActor;
    if (v.g._cameraMode().* == 4) {
        v.g.charge_skip_from_set().* = 0;
    } else if (v.g.charge_skip_from_set().* == 0) {
        v.g.charge_skip_from_set().* = 1;
        v.g.charge_skip_from().* = c.pc_vsync_count();
    }
    phase.* = psx.conv(c_int, v.g._cameraMode().*);
    actor.* = caster;
    action.* = a.actionIndex;
    skipping.* = @intFromBool(v.g.pc_qol_skip_charging().* != 0 and (caster == 0 or v.g.pc_qol_skip_charging_who().* != 0) and
        (v.g.pc_charge_skip_kinds().* & c.charge_skip_kind(a.actionIndex)) != 0 and
        v.g.D_800F4C34().* == 0 and
        v.g._cameraMode().* >= 5 and
        c.pc_vsync_count() -% v.g.charge_skip_from().* >= psx.conv(c_uint, c.charge_skip_hold_vsyncs(caster)));
    return 1;
}

/// orig: charge_skip_now (src/BATTLE/BATTLE.PRG/146C.c)
/// Whether this vsync is in a wind-up Skip takes out.
pub fn now() callconv(.c) c_int {
    var phase: c_int = undefined;
    var skipping: c_int = undefined;
    var actor: c_int = undefined;
    var action: c_int = undefined;
    return @intFromBool(c.pc_charge_skip_state(&phase, &skipping, &actor, &action) != 0 and skipping != 0);
}

/// orig: charge_skip_load (src/BATTLE/BATTLE.PRG/146C.c)
/// The effect's files loaded back to back: the loader's steps run, each file read when asked for, until it is ready.
pub fn load() callconv(.c) void {
    const context = v.g.D_800F569C().*;
    var i: c_int = 0;
    while (i < 64) : (i += 1) {
        const loader = v.g.D_800F531C().*;
        if (loader != 6 and loader != 2 and loader != 8 and loader != 3) break;
        c.func_800D7890(1);
        _ = c.func_800CE9B0();
        _ = c.pc_cd_queue_load(@ptrCast(v.g.D_800F568C().*));
    }
    v.g.D_800F569C().* = context;
    v.g.pc_charge_skip_pumped().* = i;
}

/// orig: pc_charge_skip (src/BATTLE/BATTLE.PRG/146C.c)
/// Where the game runs action phase 5: with Skip on, stops the cast sound, applies the turn, ends the magic circle, loads the effect and releases; 1 if it did.
pub fn skip(phase: c_int) callconv(.c) c_int {
    if (phase != 5 or c.charge_skip_now() == 0) return 0;
    c.func_80045D64(0x7E, 0x34);
    c.func_80045D64(0x7E, 0x35);
    c.func_80045D64(0x7E, 0x36);
    // the index is a u8, the table has 17 entries: the C does not check
    const actor = psx.at(v.g.D_800F4538(), block().unk854[block().unk0 & 3].unk4.unk0.targetActor).*;
    if (actor != null and actor.?.unk0.unk18 != 0) {
        const unk0 = &actor.?.unk0;
        unk0.pitch = psx.conv(i16, (@as(c_int, unk0.pitch) +% unk0.unk3C) & 0xFFF);
        unk0.facing = psx.conv(i16, (@as(c_int, unk0.facing) +% unk0.unk3E) & 0xFFF);
        unk0.roll = psx.conv(i16, (@as(c_int, unk0.roll) +% unk0.unk40) & 0xFFF);
        unk0.unk3C = 0;
        unk0.unk3E = 0;
        unk0.unk40 = 0;
        unk0.unk18 = 0;
    }
    c.func_800CF920();
    if (v.g.pc_charge_skip_pump().* != 0) c.charge_skip_load();
    if (c.vs_battle_getEmptyObjectDataSlot() == 0 and c.func_800CEEBC() != 0) c.func_800704B0();
    return 1;
}

/// orig: pc_charge_skip_hold (src/BATTLE/BATTLE.PRG/146C.c)
/// At the action's release: 1 if the name window stays up (shown for the setting's seconds, less the hold shown, at most the game's own countdown).
pub fn hold() callconv(.c) c_int {
    v.g.charge_skip_held().* = 0;
    if (v.g.nameWindowText().* == null or c.charge_skip_now() == 0) return 0;
    var vsyncs: c_int = floatToInt(v.g.pc_qol_skip_charging_name().* * 60.0 + 0.5);
    vsyncs -%= c.charge_skip_hold_vsyncs(block().unk854[block().unk0 & 3].unk4.unk0.targetActor);
    if (vsyncs > @as(c_int, v.g.nameWindowTimer().*)) vsyncs = v.g.nameWindowTimer().*;
    if (vsyncs <= 0) return 0;
    v.g.charge_skip_held().* = 1;
    v.g.charge_skip_held_at().* = c.pc_vsync_count();
    v.g.charge_skip_held_vsyncs().* = vsyncs;
    return 1;
}

/// orig: pc_charge_skip_name (src/BATTLE/BATTLE.PRG/146C.c)
/// The held name window closed, the game's way, once it has been up long enough and its text is out, when the action is over, or now.
pub fn keepName(now_: c_int) callconv(.c) void {
    if (v.g.charge_skip_held().* == 0) return;
    if (v.g.nameWindowText().* == null) {
        v.g.charge_skip_held().* = 0;
        return;
    }
    if (now_ == 0 and v.g.D_800F196C().* == 2 and v.g.D_800F19CC().* != null and
        (c.pc_vsync_count() -% v.g.charge_skip_held_at().* < psx.conv(c_uint, v.g.charge_skip_held_vsyncs().*) or
            psx.getBits(&v.g.textBoxes()[7].unk0, "done") == 0))
    {
        return;
    }
    v.g.charge_skip_held().* = 0;
    c.func_800CB50C();
}

/// orig: pc_charge_skip_name_window (src/BATTLE/BATTLE.PRG/146C.c)
/// Test aid: the name window's text (up to `size` bytes, at most 112), or -1 when it is not up; `held` is whether it is held.
pub fn nameWindow(out: [*]u8, size: c_int, held: *c_int) callconv(.c) c_int {
    var n: c_int = 0;
    held.* = v.g.charge_skip_held().*;
    if (v.g.nameWindowText().* == null or v.g.textBoxes()[7].state <= 0) return -1;
    const text: [*]const u8 = @ptrCast(v.g.nameWindowText().*.?);
    while (n < size and n < 112) : (n += 1) {
        out[@intCast(n)] = text[@intCast(n)]; // n >= 0 here
    }
    return n;
}

/// orig: pc_charge_skip_view (src/BATTLE/BATTLE.PRG/146C.c)
/// Test aid: the view and the caster's facing: lookAt xyz, position xyz, the camera's distance and yaw, the facing.
pub fn view(out: *[9]c_int, actor_: c_int) callconv(.c) void {
    const a: ?*v.D_800F4538_t = if (actor_ >= 0 and actor_ < 17) v.g.D_800F4538()[@intCast(actor_)] else null;
    const camera = &v.g.vs_scratch().camera;
    out[0] = camera.lookAt.vx;
    out[1] = camera.lookAt.vy;
    out[2] = camera.lookAt.vz;
    out[3] = camera.position.vx;
    out[4] = camera.position.vy;
    out[5] = camera.position.vz;
    out[6] = v.g.vs_battle_cameraCurrentSpherical().values.distance;
    out[7] = v.g.vs_battle_cameraCurrentSpherical().values.yaw;
    out[8] = if (a != null) @as(c_int, a.?.unk0.facing) & 0xFFF else -1;
}

/// orig: pc_charge_skip_battle_state (src/BATTLE/BATTLE.PRG/146C.c)
/// BATTLE's mode, its camera mode and in the action mode the actor and action (-1 otherwise); -1 with no action block.
pub fn battleState(camera: *c_int, actor: *c_int, action: *c_int) callconv(.c) c_int {
    camera.* = psx.conv(c_int, v.g._cameraMode().*);
    actor.* = -1;
    action.* = -1;
    if (v.g.D_800F19CC().* == null) return -1;
    if (v.g.D_800F196C().* == 2) {
        const a = &block().unk854[block().unk0 & 3];
        actor.* = a.unk4.unk0.targetActor;
        action.* = a.actionIndex;
    }
    return psx.conv(c_int, v.g.D_800F196C().*);
}

/// orig: pc_charge_skip_reseed (src/BATTLE/BATTLE.PRG/146C.c)
/// The random numbers from `seed`, as _initRand seeds them at boot.
pub fn reseed(seed: c_uint) callconv(.c) void {
    c.srand(seed);
    var i: usize = 0;
    while (i < 97) : (i += 1) {
        v.g.randArr().*[i] = c.rand();
    }
    v.g.randIndex().* = 96;
}

/// orig: pc_charge_skip_trace (src/BATTLE/BATTLE.PRG/146C.c)
/// Diagnostics: this vsync's camera mode, the effect loader's state, object slot 0 in use, the cast flash flags and the background effects' ids and kinds.
pub fn trace(out: [*]u8, size: c_int) callconv(.c) c_int {
    var n: c_int = c.snprintf(out, psx.conv(c_ulong, size), "%d:%u:%d:%d", psx.conv(c_int, v.g._cameraMode().*), v.g.D_800F531C().*, @as(c_int, @intFromBool(v.g.vs_battle_objectDataSlots()[0].dataType != 0)), v.g.D_800F1868().*);
    var e = v.g.D_800F54B0().*;
    while (e != null and n < size -% 16) : (e = psx.get(e.?.next)) {
        n +%= c.snprintf(psx.at(out, n), psx.conv(c_ulong, size -% n), ",%d/%d", @as(c_int, e.?.unkD1C.unk38), @as(c_int, psx.getBits(e.?, "unk14_8")));
    }
    return if (n < size) n else size -% 1;
}

/// orig: pc_charge_skip_window (src/BATTLE/BATTLE.PRG/146C.c)
/// The chain / defence ability window of the action under way (0: none open), for the tests.
pub fn window() callconv(.c) c_int {
    return if (v.g.D_800F19CC().* != null and v.g.D_800F196C().* == 2) @as(c_int, block().battleAbilityTargetWindow) else 0;
}

/// orig: pc_charge_skip_ability (src/BATTLE/BATTLE.PRG/146C.c)
/// The chain / defence input of the action under way for the tests: timer, window, attempt spent, success; 0 with no action.
pub fn ability(out: *[4]c_int) callconv(.c) c_int {
    if (v.g.D_800F19CC().* == null or v.g.D_800F196C().* != 2) return 0;
    out[0] = block().battleAbilityTimer;
    out[1] = block().battleAbilityTargetWindow;
    out[2] = block().battleAbilityInputAttempted;
    out[3] = block().battleAbilityInputSuccessful;
    return 1;
}
