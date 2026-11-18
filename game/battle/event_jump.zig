//! How an event script opcode handler says "continue at this script address". The handlers return an int to the interpreter
//! (vs_event_executeScriptThread): 0 to 4 are its own codes, and a jump used to return the target's address in that int,
//! which holds 32 bits. A jump now stores the target here and returns `code`; the interpreter takes it at once, in the same
//! step (nothing reads it across a frame). The cell is a global of the interpreter's C unit (573B8.c), as owned Zig keeps no statics.
const psx = @import("common").psx;

pub const code: c_int = 5;

inline fn cell() *[*c]u8 {
    return psx.Global([*c]u8, "vs_event_jumpTarget").ptr();
}

/// The jump: `p` is where the script goes on.
pub fn to(p: [*c]u8) c_int {
    cell().* = p;
    return code;
}

/// The interpreter's side.
pub fn take() [*c]u8 {
    return cell().*;
}
