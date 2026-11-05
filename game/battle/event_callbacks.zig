//! The event opcode table's entries (generated event_callbacks.inc, in 4A0A8.c): each calls its handler with the script pointer; the slot is unused.
const c = @import("c_4A0A8");

/// orig: pc_event_func_800B76A8 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs func_800B76A8 on the script pointer; the slot is unused.
pub fn pc_event_func_800B76A8(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.func_800B76A8(script);
}

/// orig: pc_event_func_800B810C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs func_800B810C on the script pointer; the slot is unused.
pub fn pc_event_func_800B810C(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.func_800B810C(script);
}

/// orig: pc_event_func_800B822C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs func_800B822C on the script pointer; the slot is unused.
pub fn pc_event_func_800B822C(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.func_800B822C(script);
}

/// orig: pc_event_func_800B83D4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs func_800B83D4 on the script pointer; the slot is unused.
pub fn pc_event_func_800B83D4(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.func_800B83D4(script);
}

/// orig: pc_event_func_800B8438 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs func_800B8438 on the script pointer; the slot is unused.
pub fn pc_event_func_800B8438(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.func_800B8438(script);
}

/// orig: pc_event_func_800B9A94 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs func_800B9A94 on the script pointer; the slot is unused.
pub fn pc_event_func_800B9A94(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.func_800B9A94(script);
}

/// orig: pc_event_func_800BA2E0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs func_800BA2E0 on the script pointer; the slot is unused.
pub fn pc_event_func_800BA2E0(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.func_800BA2E0(script);
}

/// orig: pc_event_func_800BA52C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs func_800BA52C on the script pointer; the slot is unused.
pub fn pc_event_func_800BA52C(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.func_800BA52C(script);
}

/// orig: pc_event_func_800BAB14 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs func_800BAB14 on the script pointer; the slot is unused.
pub fn pc_event_func_800BAB14(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.func_800BAB14(script);
}

/// orig: pc_event_func_800BADE0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs func_800BADE0 on the script pointer; the slot is unused.
pub fn pc_event_func_800BADE0(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = slot;
    return c.func_800BADE0(script);
}

/// orig: pc_event_func_800B9BF8 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcode table entry: runs func_800B9BF8; the slot is unused.
pub fn pc_event_func_800B9BF8(script: [*c]u8, slot: c_short) callconv(.c) c_int {
    _ = script;
    _ = slot;
    return c.func_800B9BF8();
}
