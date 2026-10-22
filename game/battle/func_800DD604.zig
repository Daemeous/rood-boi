//! Action choice: picks the best usable armor slot for the actor and records it
//! in the action choice (src/BATTLE/BATTLE.PRG/74088.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_74088");
const c = @import("c_74088");

/// orig: func_800DD604 (src/BATTLE/BATTLE.PRG/74088.c)
/// Chooses the usable armor slot with the highest config value and records it in the action choice.
pub fn func_800DD604(state: ?*v.actionChoiceState) callconv(.c) void {
    var best: c_uint = 0;
    const s = state.?;
    const choice: *v.actionChoice = &s.choice;
    const stats = psx.get(s.stats).?;
    // vs_main_actions viewed as actionChoiceInfo (52-byte stride): flag28 of entry `action`.
    const infos: [*]v.actionChoiceInfo = @ptrCast(v.g.actions());

    var row: c_int = 0;
    while (row < 6) : (row += 1) {
        var col: c_int = 0;
        while (col < 4) : (col += 1) {
            const r: usize = @intCast(row);
            const k: usize = @intCast(col);
            const config: *v.actionSlotConfig = &s.configs[r][k];
            const action: c_int = @as(c_int, psx.conv(u16, stats.armor[r][k].id));
            if (c.func_800E4DF8(@ptrCast(s), row, col) != 0 and
                psx.getBits(config, "usable") != 0 and
                best < config.value)
            {
                best = config.value;
                choice.score = best;
                psx.setBits(choice, "slot", psx.conv(u2, col));
                psx.setBits(choice, "category", psx.conv(u3, row));
                psx.setBits(choice, "action", psx.conv(u8, action));
                psx.setBits(&choice.flags, "flag1", 0);
                var flags: v.actionChoiceFlags = choice.flags;
                psx.setBits(&flags, "flag2", psx.getBits(psx.at(infos, action), "flag28"));
                psx.setBits(choice, "targetWeight", 128);
                psx.setBits(&flags, "flag0", 0);
                psx.setBits(&flags, "flag3", 0);
                choice.flags = flags;
            }
        }
    }
    if (best == 0) {
        psx.setBits(choice, "action", 0);
    }
}
