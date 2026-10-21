//! Per-slot action metadata for a state: range, radius, kind and flags for each
//! action in its stats (src/BATTLE/BATTLE.PRG/7CD68.c).
const psx = @import("common").psx;
const v = @import("views_7CD68");
const c = @import("c_7CD68");

/// orig: func_800E5710 (src/BATTLE/BATTLE.PRG/7CD68.c)
/// Fills the action metadata of every slot of a state from the actions in its stats.
pub fn fillActionMetadata(state: ?*v.actionMetadataState) callconv(.c) void {
    const st = state.?;
    if (st.id == 0) {
        st.maxActionCost = 1;
        return;
    }
    for (0..6) |row| {
        for (0..4) |column| {
            const id: c_int = psx.get(st.stats).?.slots[row][column].id;
            // vs_main_actions[id]: no bound check in the C; id is a u16.
            const action: *align(1) v.actionMetadataAction = @ptrCast(psx.at(v.g.actions(), id));
            const entry: *v.actionMetadataEntry = &st.entries[row][column];
            const mask: c_int = @as(c_int, 1) << @intCast((action.flags.raw >> 20) & 15);

            entry.rangeSquared = psx.conv(u32, c.func_800E5698(@ptrCast(st), id));
            // vs_gte_rsqrt takes and returns int: the u32 square is reinterpreted, the result converted back.
            const rsqrt: c_uint = psx.conv(c_uint, c.vs_gte_rsqrt(psx.conv(c_int, entry.rangeSquared)));
            psx.setBits(entry, "radius", psx.conv(u16, c.func_800E5600(psx.conv(c_int, psx.addr(st)), id, rsqrt)));

            psx.setBits(entry, "longRange", @intFromBool(entry.rangeSquared > 0x51000 and (action.flagsC & 0x20000000) == 0));

            if ((mask & 0x4198) != 0) psx.setBits(entry, "f1", 1);
            if ((mask & 0x154) != 0) psx.setBits(entry, "f2", 1);
            if ((mask & 0xA) != 0) {
                psx.setBits(entry, "kind", 1);
            } else if ((mask & 0x40A0) != 0) {
                psx.setBits(entry, "kind", 2);
            } else if ((mask & 0x1954) != 0) {
                psx.setBits(entry, "kind", 3);
            }

            const flagType: u32 = action.flags.raw & 0xE0000;
            const byte10: u8 = @as([*]const u8, @ptrCast(v.g.D_800F58BC().*.?))[10];
            const special = byte10 != 0 and flagType != 0x20000 and flagType != 0x60000 and (mask & 0x4000) != 0;
            psx.setBits(entry, "special", @intFromBool(special));

            if (flagType == 0x20000) {
                const value: c_uint = action.flags.bytes[3];
                if (@as(c_uint, st.maxActionCost) < value) {
                    st.maxActionCost = psx.conv(u8, value);
                }
            }
        }
    }
}
