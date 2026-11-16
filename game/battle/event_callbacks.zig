//! The event opcode table's entries (generated event_callbacks.inc, in 4A0A8.c): each calls its handler with the script pointer; the slot is unused.
const c = @import("c_4A0A8");

/// orig: pc_event_func_800B76A8 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs vs_event_scriptPlayActorAnimation on the script pointer; the slot is unused.
pub fn pc_event_func_800B76A8(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.vs_event_scriptPlayActorAnimation(script);
}

/// orig: pc_event_func_800B810C (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs vs_event_scriptWaitForActorFlags on the script pointer; the slot is unused.
pub fn pc_event_func_800B810C(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.vs_event_scriptWaitForActorFlags(script);
}

/// orig: pc_event_func_800B822C (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs vs_event_scriptQueueActorAnimation on the script pointer; the slot is unused.
pub fn pc_event_func_800B822C(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.vs_event_scriptQueueActorAnimation(script);
}

/// orig: pc_event_func_800B83D4 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs vs_event_scriptPlayActorAnimationAtFrame on the script pointer; the slot is unused.
pub fn pc_event_func_800B83D4(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.vs_event_scriptPlayActorAnimationAtFrame(script);
}

/// orig: pc_event_func_800B8438 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs vs_event_scriptQueueActorAnimationAtFrame on the script pointer; the slot is unused.
pub fn pc_event_func_800B8438(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.vs_event_scriptQueueActorAnimationAtFrame(script);
}

/// orig: pc_event_func_800B9A94 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs func_800B9A94 on the script pointer; the slot is unused.
pub fn pc_event_func_800B9A94(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.func_800B9A94(script);
}

/// orig: pc_event_func_800BA2E0 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs vs_event_scriptPlaySoundCommand on the script pointer; the slot is unused.
pub fn pc_event_func_800BA2E0(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.vs_event_scriptPlaySoundCommand(script);
}

/// orig: pc_event_func_800BA52C (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs vs_event_commandCrossfadeMusic on the script pointer; the slot is unused.
pub fn pc_event_func_800BA52C(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.vs_event_commandCrossfadeMusic(script);
}

/// orig: pc_event_func_800BAB14 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs vs_event_commandPlayVibrationPattern on the script pointer; the slot is unused.
pub fn pc_event_func_800BAB14(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.vs_event_commandPlayVibrationPattern(script);
}

/// orig: pc_event_func_800BADE0 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs vs_event_commandAwardMiscLoot on the script pointer; the slot is unused.
pub fn pc_event_func_800BADE0(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.vs_event_commandAwardMiscLoot(script);
}

/// orig: pc_event_func_800B9BF8 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs func_800B9BF8; the slot is unused.
pub fn pc_event_func_800B9BF8(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = script;
    _ = slot;
    return c.func_800B9BF8();
}
