//! Queues a loot node for up to two items and opens the loot screen, or
//! closes it when nothing was won (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_items_presentScriptedLoot (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Adds a loot node for the two items to the loot list and opens the loot screen, or runs the closing path when the list is empty.
pub fn vs_items_presentScriptedLoot(id0: c_int, count0: c_int, id1: c_int, count1: c_int) callconv(.c) void {
    const raw = c.vs_main_allocHeapR(@sizeOf(v.vs_battle_lootListNode));
    if (raw) |p| {
        const node: *v.vs_battle_lootListNode = @ptrCast(@alignCast(p));
        c.vs_main_bzero(node, @sizeOf(v.vs_battle_lootListNode));

        node.loot.misc[0].id = psx.conv(u16, id0);
        if ((id0 & 0xFFFF) != 0) {
            node.loot.misc[0].count = psx.conv(u8, count0);
            node.loot.misc[0].difficultyFlags = 3;
            node.actorId |= 1;
        }

        node.loot.misc[1].id = psx.conv(u16, id1);
        if ((id1 & 0xFFFF) != 0) {
            node.loot.misc[1].count = psx.conv(u8, count1);
            node.loot.misc[1].difficultyFlags = 3;
            node.actorId |= 1;
        }

        if (node.actorId != 0) {
            // The old head is read before the new node becomes the head.
            const old_head = v.g._lootListHead().*;
            node.actorId = 0xFF;
            v.g._lootListHead().* = node;
            node.next = old_head;
        } else {
            c.vs_main_freeHeapR(node);
        }
    }

    if (v.g._lootListHead().*) |head| {
        v.g.D_800F1860().* = 1;
        c.vs_menu_frameworkOpenLootListMenu(@ptrCast(head));
        v.g._cameraMode().* = 4;
        c.vs_battle_beginRewardPause();
        return;
    }
    c.vs_battle_resumeAfterRewards();
}
