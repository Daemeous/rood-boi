//! Decrements the counter at vs_effects_startedTrackCount and sets bit `bit` of the flag word at vs_effects_pendingTracks
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_effects_requeueTrack (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Decrements the counter at vs_effects_startedTrackCount and sets bit `bit` in the flag word at vs_effects_pendingTracks.
pub fn vs_effects_requeueTrack(bit: c_int) callconv(.c) void {
    v.g.vs_effects_startedTrackCount().* -%= 1;
    // The C shifts an int by `bit`; the PS1 and x86 shift by bit & 31, so keep the low 5 bits.
    const bit_u: c_uint = @bitCast(bit);
    v.g.vs_effects_pendingTracks().* |= @as(c_uint, 1) << @as(u5, @truncate(bit_u));
}
