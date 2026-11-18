//! Advances an animated texture's tick and frame, and rewrites the texture
//! coordinates of its polygon in the room geometry (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const mpd_polygon = @import("mpd_polygon.zig");

/// orig: vs_field_advanceRoomTextureFrames (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Steps an animated quad's tick and frame and writes its frame's UVs into the room geometry.
pub fn vs_field_advanceRoomTextureFrames(arg0: ?*v.vs_field_TextureFrameEffect) callconv(.c) c_int {
    const anim = arg0.?;
    const base = psx.get(v.g.vs_battle_roomData().geometrySection).?;

    anim.tick +%= 1;
    if (@as(c_int, anim.tick) > @as(c_int, anim.delay)) {
        anim.tick = 0;
        anim.frame +%= 1;
        if (@as(c_int, anim.frame) >= @as(c_int, anim.frameCount)) {
            anim.frame = 0;
        }

        // The column and row division is unsigned, as the C's (u_int) cast makes it.
        const columns: c_uint = anim.columns;
        const frame: c_uint = anim.frame;
        const u: c_int = @bitCast(@as(c_uint, anim.baseU) +% @as(c_uint, anim.frameWidth) *% (frame % columns));
        const w: c_int = @bitCast(@as(c_uint, anim.baseV) +% @as(c_uint, anim.frameHeight) *% (frame / columns));

        mpd_polygon.setUv(mpd_polygon.at(@intFromPtr(base), anim.geometryOffset), anim.u, anim.v, u, w);
    }
    return 1;
}
