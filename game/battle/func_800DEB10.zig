//! Battle slot check: matches a request against its slot's candidate list and
//! cancels or keeps the entry (src/BATTLE/BATTLE.PRG/74088.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_74088");
const c = @import("c_74088");

/// orig: func_800DEB10 (src/BATTLE/BATTLE.PRG/74088.c)
/// Checks a request against its slot's candidate list and cancels or keeps the slot's entry.
pub fn func_800DEB10(arg0: ?*v.func_800DEEA4_t) callconv(.c) void {
    const req = arg0 orelse return;
    if (req.unk44 != 0) return;

    // The C dereferences the slot before its NULL check, so a NULL slot crashes there: `.?` traps here.
    const entry = psx.at(v.g.D_800F5878(), req.unk4).*.?;
    const temp_t1 = psx.get(entry.unk54).?;
    if (temp_t1.unk9 != 24) return;

    var i: c_int = 0;
    while (i < entry.unk470) : (i +%= 1) {
        const temp_a2 = psx.get(psx.at(&entry.unk430, i).*).?;

        if (@as(c_int, req.unk0) == @as(c_int, psx.getBits(temp_a2, "unk4_5")) and
            @as(c_int, temp_t1.unkC) == @as(c_int, psx.getBits(temp_a2, "unk4_0")) and
            @as(c_int, temp_t1.unkE) == @as(c_int, psx.getBits(temp_a2, "unk4_3")))
        {
            entry.unkFB = psx.conv(u8, psx.getBits(temp_a2, "unkC_0"));

            var j: c_int = 0;
            while (j < req.unk4A) : (j +%= 1) {
                const elem = psx.at(&req.unk4C, j);

                if (elem.unk40 == 0) {
                    const actor: ?*v.D_800F45E0_t = psx.at(v.g.D_800F45E0(), elem.unk0).*;

                    if (actor == null or
                        ((@as(c_int, 0x14) >> @as(u5, psx.getBits(&actor.?.unk6C[8], "actorId"))) & 1) != 0)
                    {
                        c.func_800E4CE8(psx.cptr(c.func_800DEEA4_t2, entry));
                        return;
                    }
                }
            }
        }
    }

    c.func_800E4C8C(psx.cptr(c.func_800DEEA4_t2, entry));
}
