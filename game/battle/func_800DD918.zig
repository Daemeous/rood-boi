//! Chooses each enabled action slot's best target and writes the candidate list
//! (src/BATTLE/BATTLE.PRG/74088.c).
const psx = @import("common").psx;
const v = @import("views_74088");
const c = @import("c_74088");

/// orig: actorLimb (src/BATTLE/BATTLE.PRG/74088.c)
/// The limb entry `limb` of `stats`, addressed as the C addresses it (limbs[] field of the stats block).
fn actorLimb(stats: *v.vs_battle_actor2, limb: c_int) *align(1) v.vs_battle_uiEquipment_limb {
    const base: u32 = psx.addr(stats);
    const addr: u32 = base +% @as(u32, @bitCast(limb)) *% @sizeOf(v.vs_battle_uiEquipment_limb) +% @offsetOf(v.vs_battle_actor2, "limbs");
    return @ptrFromInt(addr);
}

/// orig: func_800DD918 (src/BATTLE/BATTLE.PRG/74088.c)
/// Scores every enabled action slot against every living actor and keeps the best candidate in the state.
pub fn func_800DD918(state: ?*v.actionChoiceState, threshold: c_int) callconv(.c) void {
    const st = state.?;
    const list: *[4]v.actionCandidateEntry = @ptrFromInt(0x1F80030C);

    c.func_800E4288(@ptrCast(list), @sizeOf(v.actionCandidateEntry));
    list[0].distanceSquared = 256;
    psx.setBits(&list[0], "threshold", psx.conv(u16, threshold));
    c.func_800DCC94(@ptrCast(st), @ptrCast(&list[0]), 0, null);
    c.func_800DC2E0(@ptrCast(&list[1]), @ptrCast(&list[0]));
    psx.setBits(&list[1], "threshold", psx.conv(u16, threshold));
    const candidate: *v.actionCandidateEntry = &list[1];

    var row: c_int = 0;
    while (row < 6) : (row += 1) {
        var col: c_int = 0;
        while (col < 4) : (col += 1) {
            const r: usize = @intCast(row);
            const k: usize = @intCast(col);
            const action: c_int = psx.conv(c_int, psx.conv(u16, psx.get(st.stats).?.armor[r][k].id));
            const config = &st.configs[r][k];
            const info = psx.at(@as([*]v.actionChoiceInfo, @ptrCast(v.g.actions())), action);
            if (action == 0 or psx.getBits(config, "enabled") == 0) continue;

            psx.setBits(&list[1], "column", psx.conv(u2, col));
            psx.setBits(&list[1], "row", psx.conv(u3, row));
            psx.setBits(&list[1], "action", psx.conv(u8, action));
            const flag: c_int = psx.conv(c_int, psx.getBits(info, "flag29"));
            psx.setBits(&list[1], "flag1", psx.conv(u1, @as(c_int, psx.getBits(info, "flag23")) | flag));
            psx.setBits(&list[1], "flag2", psx.getBits(info, "flag28"));
            psx.setBits(&list[1], "flag3", psx.getBits(config, "flag2"));
            psx.setBits(&list[1], "range", psx.getBits(config, "range"));
            list[1].distanceSquared = config.value;

            var actor: ?*v.vs_battle_actor = v.g.vs_battle_actors()[0];
            while (actor != null) : (actor = psx.get(actor.?.next)) {
                const a = actor.?;
                const id: c_uint = psx.conv(c_uint, a.id);
                if (@as(*v.actionChoiceState, @ptrCast(psx.at(v.g.D_800F5878(), id).*.?)).dead != 0) continue;

                psx.setBits(&list[1], "targetId", psx.conv(u4, id));
                psx.setBits(&list[1], "flag4", psx.conv(u1, c.func_800DC484(action, psx.conv(c_int, id))));
                psx.setBits(&list[1], "flag5", psx.conv(u1, c.func_800DC48C(action, psx.conv(c_int, id))));
                psx.setBits(&list[1], "low", @intFromBool(psx.getBits(config, "flag5") != 0 and psx.getBits(&list[1], "flag4") != 0));
                if (c.func_800E4CF4(@ptrCast(st), @ptrCast(candidate), psx.conv(c_int, id)) == 0 or
                    psx.at(psx.at(v.g.D_800F5904().*.?, id), action).* != 0) continue;

                switch (psx.getBits(config, "mode")) {
                    1 => {
                        c.func_800DCC94(@ptrCast(st), @ptrCast(candidate), 1, @ptrFromInt(psx.addr(info)));
                        c.func_800DC4F0(@ptrCast(candidate), @ptrCast(info));
                        if (list[1].score > list[0].score) {
                            c.func_800DC344(@ptrCast(st), @ptrCast(candidate));
                        }
                    },
                    2 => {
                        var last: c_int = -1;
                        c.func_800DC2E0(@ptrCast(&list[2]), @ptrCast(&list[0]));
                        c.func_800DC2E0(@ptrCast(&list[3]), @ptrCast(&list[0]));
                        var limb: c_int = 0;
                        while (limb < 6) : (limb += 1) {
                            if (actorLimb(psx.get(a.stats).?, limb).maxHp != 0) {
                                last = limb;
                                psx.setBits(&list[1], "part", psx.conv(u3, last));
                                c.func_800DCC94(@ptrCast(st), @ptrCast(candidate), 2, @ptrFromInt(psx.addr(info)));
                                const chance: c_int = psx.conv(c_int, psx.getBits(&list[1], "weight"));
                                if ((c.rand() & 127) < chance and list[1].score > list[2].score) {
                                    c.func_800DC2E0(@ptrCast(&list[2]), @ptrCast(candidate));
                                }
                                if (list[1].score > list[3].score) {
                                    c.func_800DC2E0(@ptrCast(&list[3]), @ptrCast(&list[1]));
                                }
                            }
                        }
                        var part: c_int = c.rand();
                        part = (part & 3) +% ((part >> 2) & 1) +% ((part >> 3) & 1);
                        if (actorLimb(psx.get(a.stats).?, part).maxHp == 0 and last >= 0) {
                            part = last;
                        }
                        if (part >= 0) {
                            psx.setBits(&list[1], "part", psx.conv(u3, part));
                            c.func_800DC4F0(@ptrCast(&list[1]), @ptrCast(info));
                        }
                        if (list[2].score > list[0].score) {
                            c.func_800DC344(@ptrCast(st), @ptrCast(&list[2]));
                        } else if (list[0].score < list[3].score) {
                            c.func_800DC344(@ptrCast(st), @ptrCast(&list[3]));
                        }
                    },
                    3 => {
                        const target: *v.actionChoiceState = @ptrCast(psx.at(v.g.D_800F5878(), a.id).*.?);
                        c.func_800DC2E0(@ptrCast(&list[2]), @ptrCast(&list[0]));
                        c.func_800DC2E0(@ptrCast(&list[3]), @ptrCast(&list[0]));
                        psx.setBits(&list[1], "heading", 0);
                        var pass: c_int = 0;
                        while (pass < 1) : (pass += 1) {
                            const h: c_int = target.heading;
                            const absHeading: c_int = if (h >= 0) h else -%h;
                            psx.setBits(&list[1], "heading", psx.conv(u12, absHeading));
                            c.func_800DCC94(@ptrCast(st), @ptrCast(candidate), 3, @ptrFromInt(psx.addr(info)));
                            const chance: c_int = psx.conv(c_int, psx.getBits(&list[1], "weight"));
                            if ((c.rand() & 127) < chance and list[1].score > list[2].score) {
                                c.func_800DC2E0(@ptrCast(&list[2]), @ptrCast(candidate));
                            }
                            if (list[1].score > list[3].score) {
                                c.func_800DC2E0(@ptrCast(&list[3]), @ptrCast(&list[1]));
                            }
                        }
                        c.func_800DC4F0(@ptrCast(&list[1]), @ptrCast(info));
                        if (list[2].score > list[0].score) {
                            c.func_800DC344(@ptrCast(st), @ptrCast(&list[2]));
                        } else if (list[0].score < list[3].score) {
                            c.func_800DC344(@ptrCast(st), @ptrCast(&list[3]));
                        }
                    },
                    else => {},
                }
            }
        }
    }
}
