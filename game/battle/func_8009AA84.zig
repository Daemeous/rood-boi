//! Releases weapon model slot `index`: its offsets block (unless another model shares it), its texture references, and the slot itself (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009AA84 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Releases weapon model slot `index`; returns -1 if the slot is empty.
pub fn func_8009AA84(index: c_int) callconv(.c) c_int {
    const slot = psx.at(v.g.vs_battle_wepModels(), index);
    const model = slot.* orelse return -1;
    if (model.nBones == 0) return -1;

    if (psx.getBits(model, "actorId") != 0) {
        var objData: v.vs_battle_objectData = undefined;
        objData.index = psx.conv(i8, index);
        objData.modelId = psx.conv(i16, model.modelId);

        if (index >= 4) {
            // Look for another slot that shares this model's offsets block.
            const shared = blk: {
                const wepModels = v.g.vs_battle_wepModels();
                var i: c_int = 0;
                while (i < 20) : (i +%= 1) {
                    const other = wepModels[@intCast(i)];
                    if (i != @as(c_int, objData.index) and other != null and
                        (psx.getBits(other.?, "actorId") != 0 or @as(c_int, objData.index) < 4) and
                        @as(c_int, other.?.modelId) == @as(c_int, objData.modelId))
                    {
                        objData.dataAddr = psx.conv(u32, i);
                        break :blk true;
                    }
                }
                break :blk false;
            };
            if (!shared) {
                c.vs_main_freeHeap(@ptrCast(psx.get(model.offsets)));
            }
        } else {
            c.vs_main_freeHeap(@ptrCast(psx.get(model.offsets)));
        }
    }

    // The texture slot is unchecked in the original: index without a bound.
    psx.at(v.g.vs_battle_wepTextures(), @as(c_int, model.texSlot)).refCount -%= 1;

    if (psx.getBits(model, "isWideTexture") != 0) {
        psx.at(v.g.vs_battle_wepTextures(), @as(c_int, model.texSlot) +% 1).refCount -%= 1;
    }

    psx.at(v.g.vs_battle_wepModels(), index).* = null;
    return 0;
}
