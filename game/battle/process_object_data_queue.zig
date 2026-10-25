//! Runs the next queued object-data load, times it, and shifts the queue down
//! (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_battle_processObjectDataQueue (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Runs the first queued object-data load, times it, and removes it from the queue.
pub fn processObjectDataQueue() callconv(.c) c_int {
    const slots = v.g.vs_battle_objectDataSlots();
    const slot = &slots[0];

    v.g.D_800F4580().* = c.VSync(1);

    const ret: c_int = switch (slot.dataType) {
        0 => return 0,
        1 => c._loadShp(@ptrCast(slot)),
        3 => c._loadWep(@ptrCast(slot)),
        7 => c.func_8009BE5C(@ptrCast(slot)),
        2 => c._parseShp(@ptrCast(slot)),
        4 => c._parseWep(@ptrCast(slot)),
        6 => c.func_8009998C(@ptrCast(slot)),
        8 => c._loadSeq(@ptrCast(slot)),
        9 => c._loadEtm(@ptrCast(slot)),
        else => return -1,
    };

    const now = c.VSync(1);
    v.g.D_800F4580().* = now -% v.g.D_800F4580().*;

    if (v.g._loadShpState().* == 2) {
        if (v.g.D_800E8FC4().* < v.g.D_800F4580().*) {
            v.g.D_800E8FC4().* = v.g.D_800F4580().*;
            v.g.D_800F45D8().* = psx.conv(i32, slot.dataType);
            v.g.D_800F457C().* = psx.conv(i32, v.g._loadShpState().*);
        }
    }

    if (ret == -1) return 1;

    var i: usize = 1;
    while (i < 15) : (i +%= 1) {
        c.vs_main_memcpy(&slots[i - 1], &slots[i], @sizeOf(v.vs_battle_objectData));
    }
    slots[i - 1].dataType = 0;

    return if (ret < 0) -1 else 1;
}
