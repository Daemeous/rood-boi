//! Unlinks an entry from the battle list headed at D_800F58BC (src/BATTLE/BATTLE.PRG/73700.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_73700");

/// orig: func_800DBF00 (src/BATTLE/BATTLE.PRG/73700.c)
/// Clears the entry's flag and removes it from the list at D_800F58BC, fixing up the head and tail pointers.
pub fn func_800DBF00(entry: ?*v.func_800E78F4_t) callconv(.c) void {
    const self = entry.?;
    const data = psx.get(self.unk54).?;
    const hub = v.g.D_800F58BC().*.?;

    self.unk12F = 0;
    psx.setBits(data, "unkB_2", 2);

    if (psx.get(hub.unk20) == self) {
        psx.setBits(data, "unkB_2", 2);
        const next = psx.get(psx.get(hub.unk20).?.unk1A0);
        psx.put(&hub.unk20, if (next != null) next else psx.get(hub.unk1C));
    }

    if (psx.get(hub.unk1C) == self) {
        psx.put(&hub.unk1C, psx.get(self.unk1A0));
    } else {
        var node = psx.get(hub.unk1C);
        while (node != null) : (node = psx.get(node.?.unk1A0)) {
            if (psx.get(node.?.unk1A0) == self) {
                psx.put(&node.?.unk1A0, psx.get(self.unk1A0));
                return;
            }
        }
    }

    if (psx.get(hub.unk1C) == null) {
        psx.put(&hub.unk20, null);
    }
}
