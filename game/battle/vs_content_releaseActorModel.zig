//! Object slot release: frees a battle object's model data and clears its slot
//! (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_content_releaseActorModel (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Releases object slot `slot`: frees its model data unless another slot shares it, then clears the slot. Returns -1 for an empty slot.
pub fn vs_content_releaseActorModel(slot: c_int) callconv(.c) c_int {
    if (slot >= 0x11) return -1;

    const temp = psx.at(v.g.vs_actors_actorRecords(), slot).* orelse return -1;

    if (@as(c_int, temp.unk6E6) != 0x7F) {
        var sp10: v.vs_battle_objectData = undefined;
        sp10.index = psx.conv(i8, slot);
        sp10.modelId = psx.conv(i16, temp.unk6E6);

        // Another slot (not this one) with the same model: its data is shared, so nothing is freed.
        const found: bool = blk: {
            const d = v.g.vs_actors_actorRecords();
            var i: usize = 0;
            while (i < 17) : (i += 1) {
                const ci: c_int = @intCast(i);
                const other = d[i];
                if (ci != @as(c_int, sp10.index) and other != null and
                    @as(c_int, other.?.unk6E6) == @as(c_int, sp10.modelId))
                {
                    sp10.dataAddr = psx.conv(u32, ci);
                    break :blk true;
                }
            }
            break :blk false;
        };

        if (!found) {
            c.vs_audio_stopBankSfx(0x180, @ptrFromInt(@intFromPtr(psx.get(temp.unk0.unk68).?) +% psx.get(temp.unk0.unk68).?.akaoOffset), 0);
            c.vs_main_freeHeap(psx.get(temp.unk0.unk68));
            c.vs_main_freeHeap(psx.get(temp.unk5D0));
        }

        if (psx.get(temp.unk5D4) != null) {
            const shared: bool = blk: {
                const d = v.g.vs_actors_actorRecords();
                var i: usize = 0;
                while (i < 17) : (i += 1) {
                    const ci: c_int = @intCast(i);
                    const other = d[i];
                    if (ci != slot and other != null and
                        psx.get(other.?.unk5D4) == psx.get(temp.unk5D4))
                    {
                        break :blk true;
                    }
                }
                break :blk false;
            };
            if (!shared) {
                c.vs_main_freeHeap(psx.get(temp.unk5D4));
            }
        }

        _ = c.vs_content_releaseActorSequence(slot, 0);

        if (slot < 10) {
            _ = c.vs_content_releaseWeaponModel(slot *% 2);
            _ = c.vs_content_releaseWeaponModel((slot *% 2) | 1);
        }

        if (psx.getBits(temp, "unk5B0_5") != 0) {
            var i: c_int = 0;
            while (i < @as(c_int, psx.getBits(&temp.unk0, "unk8_4")) +% 1) : (i +%= 1) {
                psx.at(v.g.vs_render_legacyActorTextureSlots(), @as(c_int, temp.unk5BB) +% i).refCount -%= 1;
            }

            if (temp.unk5BB == 20) {
                _ = c.vs_render_retireTexturePages(31, 1, 4);
            }
        }
    }

    psx.at(v.g.vs_actors_actorRecords(), slot).* = null;
    return 0;
}
