//! Frees the background layout block and closes its companion state
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_releaseTiledBackground (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Clears the flag, and if a background layout is held, closes it, frees it and returns 1; otherwise returns 0.
pub fn vs_field_releaseTiledBackground() callconv(.c) c_int {
    v.g.vs_field_isTiledBackgroundEnabled().* = 0;
    if (v.g.vs_field_loadedBackgroundLayout().* != null) {
        v.g.vs_field_isTiledBackgroundEnabled().* = 0;
        _ = c.vs_render_retireTexturePages(
            @as(c_uint, v.g.vs_field_legacyBackgroundTexturePage().*),
            @as(c_uint, @as(u32, @bitCast(@as(i32, v.g.vs_field_legacyBackgroundTextureReservationId().*)))),
            5,
        );
        c.vs_main_freeHeapR(v.g.vs_field_loadedBackgroundLayout().*);
        v.g.vs_field_loadedBackgroundLayout().* = null;
        return 1;
    }
    return 0;
}
