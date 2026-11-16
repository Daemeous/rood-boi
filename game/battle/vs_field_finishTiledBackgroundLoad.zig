//! Finishes a pending CD image load, or frees its staging buffer
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_finishTiledBackgroundLoad (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Uploads a finished TIM load and returns 0, or frees the staging buffer of a finished second load and returns 1; 0 otherwise.
pub fn vs_field_finishTiledBackgroundLoad() callconv(.c) c_int {
    if (v.g.vs_field_backgroundTextureDiscRequest().* != null and v.g.vs_field_backgroundTextureDiscRequest().*.?.state == 4) {
        var tim = std.mem.zeroes(v.TIM_IMAGE);

        v.g.vs_field_legacyBackgroundTimBuffer().*.?[0] = 0x10;
        c.vs_battle_parseTim(v.g.vs_field_legacyBackgroundTimBuffer().*.?, psx.cptr(c.TIM_IMAGE, &tim));
        if (tim.paddr) |paddr| {
            const prect = tim.prect.?;
            prect.x = psx.conv(i16, (@as(c_int, prect.x) & 0x3F) +%
                ((@as(c_int, v.g.vs_field_legacyBackgroundTexturePage().*) & 0xF) << 6));
            prect.y = psx.conv(i16, @as(c_int, psx.conv(u8, prect.y)) +%
                ((@as(c_int, v.g.vs_field_legacyBackgroundTexturePage().*) & 0x10) * 0x10));
            _ = c.LoadImage(psx.cptr(c.RECT, prect), paddr);
        }
        if (tim.caddr) |caddr| {
            c.vs_main_loadClut(@ptrCast(caddr), 0xE, 0, 0x100);
        }
        c.vs_main_freeCdQueueSlot(psx.cptr(c.vs_main_CdQueueSlot, v.g.vs_field_backgroundTextureDiscRequest().*.?));
        v.g.vs_field_backgroundTextureDiscRequest().* = null;
        return 0;
    }

    if (v.g.vs_field_backgroundLayoutDiscRequest().* != null and v.g.vs_field_backgroundLayoutDiscRequest().*.?.state == 4) {
        c.vs_main_freeHeapR(@ptrCast(v.g.vs_field_legacyBackgroundTimBuffer().*));
        v.g.vs_field_legacyBackgroundTimBuffer().* = null;
        c.vs_main_freeCdQueueSlot(psx.cptr(c.vs_main_CdQueueSlot, v.g.vs_field_backgroundLayoutDiscRequest().*.?));
        v.g.vs_field_backgroundLayoutDiscRequest().* = null;
        return 1;
    }
    return 0;
}
