//! Finishing a pending stop request (D2: 72B70.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: func_800DB7B4 (pc/src/battle_movement.c)
/// Finishes a pending stop request (0x12D): request 1 with a basic command
/// (< 9) idles (command 1); otherwise releases the tile reservation
/// (func_800E678C), clears the request and switches to command 2.
pub fn finishStopRequest(state: [*]u8) callconv(.c) void {
    const request: c_int = state[0x12d];
    if (request == 0) return;
    if (request == 1) {
        const command: [*]u8 = @ptrFromInt(psx.byteAt(u32, state, 0x54).*);
        if (command[9] < 9) {
            c.func_800DEEFC(state, 1);
            return;
        }
    }
    c.func_800E678C(state);
    state[0x12d] = 0;
    c.func_800DEEFC(state, 2);
}
