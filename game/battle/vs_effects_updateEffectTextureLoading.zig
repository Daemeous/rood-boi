//! Battle file loader step: loads the next queued file into its VRAM slot and frees the load buffer when the queue is done (pc/src/game/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");
const c = @import("c_6F014");

/// Starts or polls one CD read into `buf` (the static `_loadfile` of 6F014.c, carried here).
fn loadFile(lba: c_int, size: c_int, buf: ?*anyopaque) c_int {
    const cur = v.g.vs_effects_resourceReadSlot();
    if (cur.* == null) {
        var cd_file: c.vs_main_CdFile = undefined;
        cd_file.lba = psx.conv(@TypeOf(cd_file.lba), lba);
        cd_file.size = psx.conv(@TypeOf(cd_file.size), size);
        cur.* = @ptrCast(c.vs_main_allocateCdQueueSlot(&cd_file));
        c.vs_main_cdEnqueue(@ptrCast(cur.*), buf);
        return 1;
    }

    const slot: [*c]c.vs_main_CdQueueSlot = @ptrCast(cur.*.?);
    if (slot.*.state == c.vs_main_CdQueueStateLoaded) {
        c.vs_main_freeCdQueueSlot(slot);
        cur.* = null;
        return 0;
    }

    return 1;
}

/// orig: vs_effects_updateEffectTextureLoading (pc/src/game/BATTLE/BATTLE.PRG/6F014.c)
/// Advances the battle file loader one step: loads the next queued file into its VRAM slot, and frees the load buffer when the queue is finished.
pub fn vs_effects_updateEffectTextureLoading() callconv(.c) c_int {
    if (v.g.D_800F5680().* == 0 and v.g.vs_effects_textureLoads().unk0 != 0) {
        v.g.vs_effects_textureLoadIndex().* = 0;
        c.vs_effects_setEffectTextureLoadState(1);
    }

    const temp_s1: c_uint = v.g.D_800F5680().*;
    // Uninitialised in the original for values other than 0..2; 0 is returned here.
    var var_s0: c_int = 0;

    // A plain switch (a labelled `continue :sw` emits a writable jump table in the
    // engine library's .data): case 2 falls through to case 0's store.
    switch (temp_s1) {
        1 => {
            if (v.g.vs_effects_textureLoadBuffer().* == null) {
                v.g.vs_effects_textureLoadBuffer().* = @ptrCast(@alignCast(c.vs_main_allocHeapR(32768)));
            }

            const ctx = psx.at(&v.g.vs_effects_textureLoads().loadContexts, v.g.vs_effects_textureLoadIndex().*);
            var_s0 = loadFile(
                @as(c_int, ctx.lba) +% 115009,
                @as(c_int, ctx.size),
                @ptrCast(v.g.vs_effects_textureLoadBuffer().*),
            );

            if (var_s0 == 0) {
                var slot: c_int = @as(c_int, psx.at(&v.g.vs_effects_textureLoads().slots, v.g.vs_effects_textureLoadIndex().*).*);
                var sp50: v.RECT = undefined;

                if (psx.at(&v.g.vs_effects_textureLoads().isClut, v.g.vs_effects_textureLoadIndex().*).* == 0) {
                    slot +%= 24;
                    _ = c.vs_render_reserveTexturePages(psx.conv(c_uint, slot), 1, 2);
                    // x86 shifts count by the low 5 bits.
                    const sh: u5 = @truncate(psx.conv(c_uint, slot));
                    v.g.D_800F5874().* |= @bitCast(@as(c_uint, temp_s1) << sh);
                    sp50.x = psx.conv(i16, (slot -% @divTrunc(slot, 16) *% 16) << 6);
                    sp50.y = psx.conv(i16, @divTrunc(slot, 16) << 8);
                    sp50.w = 64;
                    sp50.h = 256;
                    _ = c.LoadImage(psx.cptr(c.RECT, &sp50), @ptrCast(v.g.vs_effects_textureLoadBuffer().*));
                } else {
                    sp50.x = 768;
                    sp50.y = psx.conv(i16, slot +% 240);
                    sp50.w = 256;
                    sp50.h = psx.conv(i16, 5 -% slot);
                    _ = c.LoadImage(psx.cptr(c.RECT, &sp50), @ptrCast(v.g.vs_effects_textureLoadBuffer().*));
                }

                v.g.vs_effects_textureLoadIndex().* +%= 1;

                if (v.g.vs_effects_textureLoads().unk0 == v.g.vs_effects_textureLoadIndex().*) {
                    c.vs_effects_setEffectTextureLoadState(2);
                }

                var_s0 = 1;
            }
        },
        2 => {
            if (v.g.vs_effects_textureLoadBuffer().* != null) {
                c.vs_main_freeHeapR(@ptrCast(v.g.vs_effects_textureLoadBuffer().*));
                v.g.vs_effects_textureLoadBuffer().* = null;
            }
            var_s0 = 0;
        },
        0 => {
            var_s0 = 0;
        },
        else => {},
    }

    return var_s0;
}
