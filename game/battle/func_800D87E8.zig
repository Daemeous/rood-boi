//! Actor relation update: distance, range and height gates, view, line-of-sight blocking and the kind timer toward actor `id` (src/BATTLE/BATTLE.PRG/6FC00.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6FC00");
const c = @import("c_6FC00");

/// vs_battle_actors[id]->unk44 as an actorObject (psx_get, then the cast).
fn objectOf(id: c_int) *v.actorObject {
    const actor = psx.at(v.g.vs_battle_actors(), id).*.?;
    const model = psx.get(actor.unk44).?;
    return @ptrCast(@alignCast(model));
}

/// (actorObject*)D_800F4538[index].
fn modelObject(index: c_int) *v.actorObject {
    const model = psx.at(v.g.D_800F4538(), index).*.?;
    return @ptrCast(@alignCast(model));
}

/// RELATION_STATES(id): the relation state in the scratchpad slot, or NULL.
fn relationStates(id: c_int) ?*v.actorRelationState {
    const slot_addr: u32 = 0x1F80037C +% psx.conv(u32, id) *% 4;
    const word = @as(*const u32, @ptrFromInt(@as(usize, slot_addr))).*;
    if (word == 0) return null;
    return @ptrFromInt(@as(usize, word));
}

/// checkExtent (6FC00.c): range test of the pair against the other state's extent.
fn checkExtent(from: *v.SVECTOR, to: *v.SVECTOR, state: *v.actorRelationState, id: c_int) c_int {
    const other = relationStates(id);
    var extent: c_int = 128;
    if (other) |o| extent = @as(c_int, o.extent);
    return c.func_800E44E8(@ptrCast(from), @ptrCast(to), extent, @as(c_int, state.extent));
}

/// checkHeight (6FC00.c): height test of the pair against the other state's height.
fn checkHeight(from: *v.SVECTOR, to: *v.SVECTOR, state: *v.actorRelationState, id: c_int) c_int {
    const other = relationStates(id);
    var height: c_int = 64;
    if (other) |o| {
        height = @as(c_int, o.height);
        if (height < 0) height = -%height;
    }
    var own: c_int = @as(c_int, state.height);
    if (own < 0) own = -%own;
    return c.func_800E44E8(@ptrCast(from), @ptrCast(to), height, own);
}

/// orig: func_800D87E8 (src/BATTLE/BATTLE.PRG/6FC00.c)
/// Updates actor `state`'s relation toward actor `id`: range, view, blocking and kind timer.
pub fn func_800D87E8(state: ?*v.actorRelationState, id: c_int) callconv(.c) void {
    const s = state.?;

    // Height of the target: its extent when it is airborne above us, else its height.
    var height: c_int = -64;
    const target: ?*v.actorRelationState = @ptrCast(psx.at(v.g.D_800F5878(), id).*);
    if (target) |t| {
        const targetObject = modelObject(id);
        if ((targetObject.flags.raw & 0x180000) != 0 and
            @as(c_int, targetObject.position.vy) > @as(c_int, s.position.vy))
        {
            height = -%@as(c_int, t.extent);
        } else {
            height = @as(c_int, t.height);
        }
    }
    psx.at(&s.distances, id).* = psx.conv(u32, c.func_800E4624(
        @ptrCast(&s.position),
        @ptrCast(&objectOf(id).position),
        @as(c_int, s.height),
        height,
    ));

    // Same tile, or a moving object whose last tile matches: always in reach.
    const rel = psx.at(&s.relations, id);
    var object: *v.actorObject = undefined;
    var same_tile = true;
    if (psx.at(v.g.D_800F45E0(), id).* == null) {
        object = objectOf(id);
        same_tile = (object.tile.raw & 0xFF00FF) == (s.tile.raw & 0xFF00FF) or
            ((object.flags.raw & 0x180000) != 0 and
                (object.lastTile.raw & 0xFF00FF) == (s.tile.raw & 0xFF00FF));
    }
    if (same_tile) {
        rel.raw |= 0x40000004;
    } else {
        s.phase +%= 1;
        if ((@as(c_int, s.phase) & 1) != 0) {
            psx.setBits(&rel.bits, "outOfReach", @intFromBool(checkExtent(&object.position, &s.position, s, id) == 0));
        } else {
            psx.setBits(&rel.bits, "hidden", @intFromBool(checkHeight(&object.position, &s.position, s, id) == 0));
        }
        if (psx.getBits(&rel.bits, "hidden") != 0) {
            psx.setBits(&rel.bits, "outOfReach", 1);
        }
    }

    // In view: the angle from our facing to the target is at least the limit.
    {
        const own = psx.get(s.object).?;
        const view = c.func_800E437C(
            @ptrCast(&s.position),
            @ptrCast(&objectOf(id).position),
            @as(c_int, own.facing) +% @as(c_int, own.unk14),
            psx.at(&s.distances, id).*,
        );
        psx.setBits(&rel.bits, "inView", @intFromBool(view >= psx.conv(c_int, s.angleLimit)));
    }

    var relation: v.actorRelation = rel.*;
    if (@as(c_int, s.actorId) == id) {
        psx.setBits(&relation.bits, "active", 1);
        psx.setBits(&relation.bits, "visible", 1);
        psx.setBits(&relation.bits, "previous", 0);
    } else {
        var active: c_int = 0;
        if (psx.at(&s.distances, id).* <= s.farDistance and
            psx.getBits(&relation.bits, "inView") != 0 and
            id != @as(c_int, s.actorId) and
            (s.unk25 != 0 or psx.getBits(&relation.bits, "outOfReach") != 0))
        {
            active = 1;
        }
        psx.setBits(&relation.bits, "active", psx.conv(u1, active));

        const timer = psx.getBits(&relation.bits, "timer");
        if (timer != 0) {
            psx.setBits(&relation.bits, "timer", psx.conv(u5, @as(c_int, timer) -% 1));
            psx.setBits(&relation.bits, "active", 1);
        }

        var previous: c_int = 0;
        if (psx.getBits(&relation.bits, "active") == 0) {
            previous = @as(c_int, psx.getBits(&rel.bits, "active"));
        }
        psx.setBits(&relation.bits, "previous", psx.conv(u1, previous));

        var visible: c_int = 0;
        if (psx.getBits(&relation.bits, "active") != 0 or
            (psx.at(&s.distances, id).* < s.nearDistance and
                psx.getBits(&relation.bits, "outOfReach") != 0))
        {
            visible = 1;
        }
        psx.setBits(&relation.bits, "visible", psx.conv(u1, visible));
    }

    // Blocked when an actor nearer than the target sits on the line toward it.
    {
        const pos = objectOf(id);
        const map_addr: u32 = @as(*const u32, @ptrFromInt(0x1F8003C0)).*;
        // Rows of 32 cells, indexed unchecked as the C does (z * 32 + x: a tile byte past 31 reads on).
        const cells: [*]u16 = @ptrFromInt(@as(usize, map_addr));
        const z: usize = psx.getBits(&pos.tile.p, "z");
        const x: usize = psx.getBits(&pos.tile.p, "x");
        if ((cells[z * 32 + x] & 0x2000) != 0) {
            psx.setBits(&relation.bits, "unk31", 1);
        } else {
            psx.setBits(&relation.bits, "unk31", psx.getBits(&relation.bits, "active"));
        }
    }
    rel.* = relation;

    var blocked: c_int = 0;
    var radius: u16 = 63;
    const own_pos = psx.get(s.object).?;
    const x: c_int = @as(c_int, own_pos.position.vx);
    const z: c_int = @as(c_int, own_pos.position.vz);
    const owner: c_int = @as(c_int, s.actorId);
    const targetDistance: c_uint = psx.at(&s.distances, id).*;
    const target_obj = objectOf(id);
    const heading: c_int = c.ratan2(
        @as(c_int, target_obj.position.vx) -% x,
        @as(c_int, target_obj.position.vz) -% z,
    );
    const other = relationStates(id);
    if (other) |o| {
        radius = o.radius;
    }

    var actor: ?*v.vs_battle_actor = v.g.vs_battle_actors()[0];
    while (actor) |a| : (actor = psx.get(a.next)) {
        const candidate: c_int = a.id;
        if (candidate == owner or candidate == id or
            psx.at(&s.distances, candidate).* > targetDistance)
        {
            continue;
        }
        const cand_obj = modelObject(candidate);
        const angle: c_int = c.ratan2(
            @as(c_int, cand_obj.position.vx) -% x,
            @as(c_int, cand_obj.position.vz) -% z,
        ) -% heading;
        if (c.rcos(angle) < 0) continue;
        // vs_gte_rsqrt takes and returns int: the u32 distance goes in as C converts it, the result is stored as u_int.
        const root: c_uint = psx.conv(c_uint, c.vs_gte_rsqrt(psx.conv(c_int, psx.at(&s.distances, candidate).*)));
        var distance: c_int = psx.conv(c_int, root *% psx.conv(c_uint, c.rsin(angle))) >> 12;
        if (distance < 0) distance = -%distance;
        if (psx.conv(c_uint, distance) < @as(c_uint, radius)) {
            blocked = 1;
            break;
        }
    }

    var result: v.actorRelation = undefined;
    result.raw = rel.raw & 0xF7FFFFFF;
    result.raw |= psx.conv(c_uint, blocked) << 27;
    rel.* = result;

    if (@as(c_int, s.actorId) != id and relationStates(id) != null) {
        if (psx.getBits(&result.bits, "kindTimer") != 0) {
            const kt = psx.conv(u7, @as(c_int, psx.getBits(&result.bits, "kindTimer")) -% 1);
            psx.setBits(&result.bits, "kindTimer", kt);
            if (kt == 0) {
                psx.setBits(&result.bits, "kind", psx.getBits(&result.bits, "previousKind"));
            }
        }
        if (s.unk22 != 0 and psx.getBits(&result.bits, "kind") == 3) {
            if (psx.getBits(&result.bits, "active") != 0) {
                psx.setBits(&result.bits, "counter", 0);
            } else {
                const counter = psx.conv(u7, @as(c_int, psx.getBits(&result.bits, "counter")) +% 1);
                psx.setBits(&result.bits, "counter", counter);
                if (@as(c_int, counter) >= 121) {
                    psx.setBits(&result.bits, "kind", 1);
                }
            }
        }
        rel.* = result;
    }
}
