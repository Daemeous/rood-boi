//! Resetting an actor's forwarded values (D2: 6FA0C.s; oracle pc/src/actor_cleanup.c).
const c = @import("c_actor_cleanup");

/// orig: func_800D821C (pc/src/actor_cleanup.c)
/// Sends the actor's id (byte 0x88; none when 0) the values 0 and 0x40.
pub fn resetActorValues(actor: [*]u8) callconv(.c) void {
    const id: c_uint = actor[0x88];
    if (id != 0) {
        c.func_80095B7C(@bitCast(id), 0);
        c.func_80095B7C(@bitCast(id), 0x40);
    }
}
