//! Battle slot check: matches a request against its slot's candidate list and
//! cancels or keeps the entry (pc/src/game/BATTLE/BATTLE.PRG/74088.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_74088");
const c = @import("c_74088");

/// orig: vs_battle_aiReconcileCompletedActionPlan (pc/src/game/BATTLE/BATTLE.PRG/74088.c)
/// Checks a request against its slot's candidate list and cancels or keeps the slot's entry.
pub fn vs_battle_aiReconcileCompletedActionPlan(arg0: ?*v.func_800DEEA4_t) callconv(.c) void {
    const req = arg0 orelse return;
    if (req.unk44 != 0) return;

    // The C dereferences the slot before its NULL check, so a NULL slot crashes there: `.?` traps here.
    const entry = psx.at(v.g.vs_battle_aiActorAiStates(), req.unk4).*.?;
    const temp_t1 = entry.actor.?;
    const temp_c: u32 = @bitCast(temp_t1.unkC);
    if (temp_t1.unk9 != 24) return;

    var i: c_int = 0;
    while (i < entry.count470) : (i +%= 1) {
        const temp_a2 = psx.get(psx.at(&entry.entries.actions, i).*).?;

        if (@as(c_int, req.unk0) == @as(c_int, psx.getBits(temp_a2, "unk4_5")) and
            @as(c_int, @as(u16, @truncate(temp_c))) == @as(c_int, psx.getBits(temp_a2, "unk4_0")) and
            @as(c_int, @as(u16, @truncate(temp_c >> 16))) == @as(c_int, psx.getBits(temp_a2, "unk4_3")))
        {
            entry.unkFB = psx.conv(u8, psx.getBits(temp_a2, "unkC_0"));

            var j: c_int = 0;
            while (j < req.unk4A) : (j +%= 1) {
                const elem = psx.at(&req.unk4C, j);

                if (elem.unk40 == 0) {
                    const actor: ?*v.D_800F45E0_t = psx.at(v.g.vs_field_gimmickInstances(), elem.unk0).*;

                    if (actor == null or
                        ((@as(c_int, 0x14) >> @as(u5, psx.getBits(&actor.?.unk6C[8], "actorId"))) & 1) != 0)
                    {
                        c.vs_battle_aiForgetAiActionTarget(@ptrCast(entry));
                        return;
                    }
                }
            }
        }
    }

    c.vs_battle_aiRememberAiActionTarget(@ptrCast(entry));
}
