//! The event script's actor-reference set (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BB68C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Fills set with the actors a script reference names: with bit 0x2000, every live actor slot
/// (0x2003: only the hostile ones); otherwise the one actor func_800BFE50 resolves.
pub fn func_800BB68C(ref: u16, set: *v.func_800BB68C_t) callconv(.c) void {
    var count: i16 = 0;
    set.unk0 = 0;
    set.unk1 = 0;
    if ((ref & 0x2000) != 0) {
        var allegiance: i16 = -1;
        if (ref == 0x2003) allegiance = 4;
        var i: i16 = 0;
        while (i < 17) : (i += 1) { // i < 17: no overflow
            if (c.func_8007CF64(i)) |p| {
                const actor: *v.vs_battle_actor = @ptrCast(@alignCast(p));
                if (allegiance < 0 or @as(c_int, actor.allegiance) == allegiance) {
                    set.unk2[@intCast(count)] = psx.conv(u8, i); // count <= i < 17: inside unk2[22]
                    count += 1;
                    set.unk0 +%= 1;
                }
            }
        }
        return;
    }
    set.unk0 +%= 1;
    set.unk2[0] = psx.conv(u8, c.func_800BFE50(ref));
}
