//! Refreshes the action-dispatch state of each battle actor on the actor list
//! (src/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: func_800DAED0 (src/BATTLE/BATTLE.PRG/71618.c)
/// Updates every listed actor's dispatch state: its distance to the target, or a reset of the state with its terrain.
pub fn func_800DAED0() callconv(.c) void {
    const head: *v.actionDispatchActor = @ptrCast(v.g.vs_battle_actors()[0].?);
    var actor = psx.get(head.next);
    while (actor) |a| {
        // Per-actor state pointer: the scratchpad table at 0x1F80037C, indexed by actor id.
        const slot_addr: usize = 0x1F80037C +% @as(usize, @bitCast(@as(isize, @as(c_int, 4) *% a.id)));
        const slot: *const u32 = @ptrFromInt(slot_addr);
        const state: *v.actionDispatchState = @ptrFromInt(@as(usize, slot.*));

        if (state.f14 == 0 or state.f1D != 0) {
            const value = c.func_800E45B4();
            if (v.g.D_800F16F4().* < value) {
                c.func_800DEEFC(@ptrCast(state), 17);
            } else {
                @as(*c_int, @ptrFromInt(0x1F8003FC)).* = value;
                if (state.f14 != 0 and state.f1D != 0) {
                    const height: c_int = blk: {
                        if (v.g.D_800F5878()[0]) |tail_raw| {
                            const tail: *v.actionDispatchState = @ptrCast(tail_raw);
                            const model: *v.actionDispatchModel = @ptrCast(v.g.D_800F4538()[0].?);
                            if ((model.flags & 0x180000) != 0 and state.position[1] < model.position[1])
                                break :blk -%@as(c_int, tail.depth);
                            break :blk @as(c_int, tail.height);
                        }
                        break :blk -64;
                    };
                    state.distance = psx.conv(u32, c.func_800E4624(
                        &state.position,
                        &psx.get(head.model).?.position,
                        @as(c_int, state.height),
                        height,
                    ));
                    _ = c.func_800DAD9C(psx.cptr(c.actionDispatchState, state));
                } else {
                    state.target = 16;
                    state.target48 = -1;
                    state.flagsC = 0;
                    state.flags0 = 0;
                    state.flags4 = 0;
                    state.flags8 = 0;
                    const table_word: *const u32 = @ptrFromInt(0x1F8003C0);
                    // Rows of 32 cells, indexed unchecked as the C does (z * 32 + x: a tile byte past 31 reads on).
                    const cells: [*]u16 = @ptrFromInt(@as(usize, table_word.*));
                    const terrain = psx.conv(c_int, cells[@as(usize, state.tileZ) * 32 + state.tileX] & 15);
                    state.terrain154 = psx.conv(i8, terrain);
                    state.terrain132 = psx.conv(i8, terrain);
                    @as(*c_int, @ptrFromInt(0x1F8003D0)).* = 0;
                    c.func_800DB820(psx.cptr(c.actionDispatchState, state));
                    c.func_800DB7B4(psx.cptr(c.actionDispatchState, state));
                }
            }
        }
        actor = psx.get(a.next);
    }
}
