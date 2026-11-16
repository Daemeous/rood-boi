//! Refreshes the action-dispatch state of each battle actor on the actor list
//! (pc/src/game/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: vs_battle_aiUpdateActorDecisions (pc/src/game/BATTLE/BATTLE.PRG/71618.c)
/// Updates every listed actor's dispatch state: its distance to the target, or a reset of the state with its terrain.
pub fn vs_battle_aiUpdateActorDecisions() callconv(.c) void {
    const head: *v.vs_battle_actor = @ptrCast(v.g.vs_battle_actors()[0].?);
    var actor = psx.get(head.next);
    while (actor) |a| {
        // Per-actor state pointer: the scratchpad table at 0x1F80037C, indexed by actor id.
        const slot_addr: usize = pad.addr(0x37C) +% @as(usize, @bitCast(@as(isize, @as(c_int, 4) *% a.id)));
        const slot: *const u32 = @ptrFromInt(slot_addr);
        const state: *v.vs_battle_aiState = @ptrFromInt(@as(usize, slot.*));

        if (state.unk14 == 0 or state.unk1D != 0) {
            const value = c.vs_core_elapsedFrameCounterTicks();
            if (v.g.D_800F16F4().* < value) {
                c.vs_battle_aiIssueActorCommand(@ptrCast(state), 17);
            } else {
                @as(*c_int, @ptrFromInt(pad.addr(0x3FC))).* = value;
                if (state.unk14 != 0 and state.unk1D != 0) {
                    const height: c_int = blk: {
                        if (v.g.vs_battle_aiActorAiStates()[0]) |tail_raw| {
                            const tail: *v.vs_battle_aiState = @ptrCast(tail_raw);
                            const model: *v.actionDispatchModel = @ptrCast(v.g.vs_actors_actorRecords()[0].?);
                            if ((model.flags & 0x180000) != 0 and state.position.xyz[1] < model.position[1])
                                break :blk -%@as(c_int, tail.extent);
                            break :blk @as(c_int, tail.height);
                        }
                        break :blk -64;
                    };
                    state.tbl.t.distances[0] = psx.conv(u32, c.vs_core_mathSquaredDistanceWithHeightOffsets(
                        &state.position.xyz,
                        @ptrCast(&@as(*v.actionDispatchModel, @ptrCast(psx.get(head.unk44).?)).position),
                        @as(c_int, state.height),
                        height,
                    ));
                    _ = c.vs_battle_aiTryExecutePlannedActions(@ptrCast(state));
                } else {
                    state.movementTargetId = 16;
                    state.previous48.target48 = -1;
                    state.flagsC.word = 0;
                    state.flags0.word = 0;
                    state.flags4.word = 0;
                    state.flags8.word = 0;
                    const table_word: *const u32 = @ptrFromInt(pad.addr(0x3C0));
                    // Rows of 32 cells, indexed unchecked as the C does (z * 32 + x: a tile byte past 31 reads on).
                    const cells: [*]u16 = @ptrFromInt(@as(usize, table_word.*));
                    const terrain = psx.conv(c_int, cells[@as(usize, state.tile.b[2]) * 32 + state.tile.b[0]] & 15);
                    state.area = psx.conv(u8, terrain);
                    state.terrain132 = psx.conv(i8, terrain);
                    @as(*c_int, @ptrFromInt(pad.addr(0x3D0))).* = 0;
                    c.vs_battle_aiDispatchMovementBehavior(@ptrCast(state));
                    c.func_800DB7B4(@ptrCast(state));
                }
            }
        }
        actor = psx.get(a.next);
    }
}
