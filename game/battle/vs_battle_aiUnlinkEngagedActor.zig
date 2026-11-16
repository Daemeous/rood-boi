//! Unlinks an entry from the battle list headed at vs_battle_aiRoomAiState (pc/src/game/BATTLE/BATTLE.PRG/73700.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_73700");

/// orig: vs_battle_aiUnlinkEngagedActor (pc/src/game/BATTLE/BATTLE.PRG/73700.c)
/// Clears the entry's flag and removes it from the list at vs_battle_aiRoomAiState, fixing up the head and tail pointers.
pub fn vs_battle_aiUnlinkEngagedActor(entry: ?*v.vs_battle_aiState) callconv(.c) void {
    const self = entry.?;
    const data = psx.get(self.actor).?;
    const hub = v.g.vs_battle_aiRoomAiState().*.?;

    self.phase12F = 0;
    data.unkB = (data.unkB & 0xf3) | (2 << 2);

    if (psx.get(hub.unk20) == self) {
        data.unkB = (data.unkB & 0xf3) | (2 << 2);
        const next = psx.get(psx.get(hub.unk20).?.next1A0);
        psx.put(&hub.unk20, if (next != null) next else psx.get(hub.unk1C));
    }

    if (psx.get(hub.unk1C) == self) {
        psx.put(&hub.unk1C, psx.get(self.next1A0));
    } else {
        var node = psx.get(hub.unk1C);
        while (node != null) : (node = psx.get(node.?.next1A0)) {
            if (psx.get(node.?.next1A0) == self) {
                psx.put(&node.?.next1A0, psx.get(self.next1A0));
                return;
            }
        }
    }

    if (psx.get(hub.unk1C) == null) {
        psx.put(&hub.unk20, null);
    }
}
