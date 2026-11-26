//! Advances one animation track over the caller's frame range (pc/src/game/BATTLE/BATTLE.PRG/44F14.c). The stream is read by native_model's track stepper
//! (pc_nm_track_step: the one reading of the packet format, shared with the studio); the track's state sits in the actor's pose block as the PS1 kept it,
//! the value's pad holding the position in the stream and the delta vector's (0x2A0 bytes on) the frame the delta lasts to. This function is the only code
//! that maps between the two.
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// orig: vs_actors_decodeAnimationTrack (pc/src/game/BATTLE/BATTLE.PRG/44F14.c)
/// Advances an SVECTOR's animation stream over the caller's frame range, applying each frame's deltas.
pub fn vs_actors_decodeAnimationTrack(offset: c_int, value: ?*v.SVECTOR, rotation: c_int, frames: ?*c.animationFrameScratch) callconv(.c) void {
    const scratch = frames.?;
    const val = value.?;
    const delta = psx.byteAt(v.SVECTOR, val, 0x2A0);

    const table: [*c]const u8 = @ptrCast(psx.get(scratch.data));
    var track: c.pc_nm_track = .{
        .stream = c.pc_nm_table_data(table) + @as(usize, psx.conv(u32, offset)),
        .length = 0x4000_0000, // (the game's reads are not bounded)
        .pos = @as(c_uint, psx.conv(u16, val.pad)),
        .next = @as(c_int, psx.conv(u16, delta.pad)),
        .delta = .{ delta.vx, delta.vy, delta.vz },
        .val = .{ val.vx, val.vy, val.vz },
        .live = 1,
        .ended = 0,
    };

    var frame: c_int = psx.conv(c_int, scratch.first);
    while (frame <= psx.conv(c_int, scratch.last)) : (frame +%= 1) {
        c.pc_nm_track_step(&track, frame, rotation);
        if (track.ended != 0) {
            // A stream that ends: the frames applied so far stay, the position and frame are not stored (as the C returned).
            delta.vx = track.delta[0];
            delta.vy = track.delta[1];
            delta.vz = track.delta[2];
            val.vx = track.val[0];
            val.vy = track.val[1];
            val.vz = track.val[2];
            return;
        }
    }

    delta.vx = track.delta[0];
    delta.vy = track.delta[1];
    delta.vz = track.delta[2];
    val.vx = track.val[0];
    val.vy = track.val[1];
    val.vz = track.val[2];
    val.pad = psx.conv(i16, @as(c_int, @intCast(track.pos)));
    delta.pad = psx.conv(i16, track.next);
}
