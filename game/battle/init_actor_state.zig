//! Battle actor setup: records an actor's kind and, for an active actor, builds its initialization state (pc/src/game/BATTLE/BATTLE.PRG/7CD68.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_7CD68");
const c = @import("c_7CD68");

/// orig: vs_battle_aiCreateActorAiState (pc/src/game/BATTLE/BATTLE.PRG/7CD68.c)
/// Registers an actor for battle and, when it is active, builds its initialization state; returns 1 when the state is a placed one.
pub fn initActorState(actor: ?*v.vs_battle_actor, options: c_int) callconv(.c) c_int {
    const a = actor.?;
    const id: c_int = a.id;
    const so: i16 = psx.conv(i16, options);

    if ((a.allegiance & 0x15) != 0) {
        // 1 << id: the x86 shift takes the count modulo 32.
        const sh: u5 = @truncate(@as(c_uint, @bitCast(id)));
        const bit: i32 = @bitCast(@as(c_uint, 1) << sh);
        v.g.D_800F590C().* |= bit;
    }

    if (a.unk40 == 0) {
        const object: *v.D_800F4538_t = @ptrCast(@alignCast(psx.get(a.unk44).?));
        // The kind word sits 0x100 bytes into the object's bone records.
        const kind = psx.fieldAt(c_uint, &object.bones, 0x100).* & 7;
        if (kind == 2 or kind == 4) {
            const table = v.g.vs_battle_aiRoomAiState().*.?;
            for (0..4) |k| {
                // The first free slot of the room record's registry of such actors (the PS1's word at +0x28 + 4 * k).
                if (table.registered[k] == null) {
                    table.registered[k] = a;
                    break;
                }
            }
        }
    }

    if ((a.allegiance & 5) == 0) return 0;

    const state: *v.vs_battle_aiState = @ptrCast(@alignCast(c.vs_main_allocHeap(@sizeOf(v.vs_battle_aiState)).?));
    psx.at(v.g.vs_battle_aiActorAiStates(), id).* = @ptrCast(state);
    c.vs_core_memoryClearAlignedWords(state, @sizeOf(v.vs_battle_aiState));
    state.id = psx.conv(u8, id);
    psx.put(&state.actor, a);
    const model: *v.D_800F4538_t = @ptrCast(@alignCast(psx.get(a.unk44).?));
    psx.put(&state.object.D_800F4538_t, model);
    state.stats.actorInitializationStats = @ptrCast(@alignCast(psx.get(a.stats)));

    // Uninitialised in the C; vs_actors_anchorPoint's selector 250 (0xFA) always writes it.
    var result: [4]u16 = undefined;
    c.vs_actors_anchorPoint(state.id, 250, &result, 0);
    state.height = psx.conv(i16, @as(c_int, result[1]) -% @as(c_int, model.unk0.position.vy));

    var radius: c_uint = model.unk63C;
    if (id == 0) {
        state.radius = 40;
    } else if (radius < 63) {
        state.radius = psx.conv(u16, radius);
    } else {
        state.radius = 62;
    }
    if (radius > 128) {
        state.duration = 32;
        state.collisionRadius = psx.conv(u16, radius);
    } else {
        radius = state.radius;
        state.duration = 128;
        state.collisionRadius = psx.conv(u16, radius);
    }

    psx.put(&state.position60.shorts, &model.unk0.position.vx);
    psx.put(&state.tilePointer.words, psx.fieldAt(u32, &model.unk0.currentTileX, 0));
    state.unk89 = 0;
    state.field8A = 0;
    state.direction = 1;
    state.field2A = @intFromBool(c.func_800A152C(id, 2, 3) >= 0);

    const stats = psx.get(a.stats).?;
    state.field224 = stats.unk30;
    state.field225 = stats.unk32;
    state.field226 = stats.unk31;
    state.field227 = stats.unk33;
    if ((state.field224 & 8) != 0) {
        state.distance = @as(c_int, state.field226) << 7;
    } else if ((state.field225 & 8) != 0) {
        state.distance = @as(c_int, state.field227) << 7;
    }
    c.func_800E6828(@ptrCast(state));

    var range: c_uint = state.field226;
    if (range < state.field227) range = state.field227;
    state.step = psx.conv(u8, range);

    var packed_: c_int = a.unk38;
    if ((@as(c_uint, @bitCast(a.unk34)) & 1) != 0) {
        c.func_800E5A74(&packed_, @ptrCast(&a.flags30));
    }
    _ = c.vs_battle_aiApplyActorAiSettings(@ptrCast(state), @ptrCast(&packed_));
    c.vs_battle_aiInitializeActorAiRelations(@ptrCast(state));

    const temp: c_int = state.distance -% 64;
    if (state.rangeSquared < psx.conv(c_uint, temp *% temp)) {
        state.distance = c.vs_gte_rsqrt(psx.conv(c_int, state.rangeSquared)) -% 64;
    }
    state.clearance = -256;
    state.heightBE = -128;
    state.extent = @bitCast(model.menuCameraHeightOffset);
    state.tile.sraw = psx.conv(i32, psx.get(state.tilePointer.words).?.*);
    state.field110 = psx.conv(i32, psx.get(state.tilePointer.words).?.*);
    state.field194 = -1;
    state.field9C = -1;
    state.moveY = @as(c_int, psx.at(psx.get(state.position60.shorts).?, 1).*) << 12;

    if (id != 0) {
        if ((state.flags & 0x30) != 0 and (state.flags & 0xC6) != 0 and
            (psx.at(psx.at(v.g.D_800F58B8().*.?, model.unk0.currentTileZ), model.unk0.currentTileX).* & 0x10) != 0 and
            (common.object.flags8(model).* & 0x200000) == 0)
        {
            state.next.nextView = psx.conv(i32, psx.fieldAt(u32, &model.unk0.currentTileX, 0).*);
            c.vs_battle_aiIssueActorCommand(@ptrCast(state), 12);
        } else if (so < 0 or !((@as(c_int, so) << 26) < 0)) {
            if (a.unk1F != 0) {
                c.vs_battle_aiIssueActorCommand(@ptrCast(state), 1);
                state.mode12D = 1;
            }
        }
    }

    a.unk9 = 0;
    a.unkB &= 0xf0;
    state.unk124 = a.unk9;
    state.previous.previousView = -1;
    state.options = so;

    c.func_800E72D0();
    if (state.id != 0) {
        while (c.vs_battle_aiRebuildActorActionPlans(null) != 0) {}
    }

    return @intFromBool(so >= 0 and (@as(c_int, so) << 26) < 0);
}
