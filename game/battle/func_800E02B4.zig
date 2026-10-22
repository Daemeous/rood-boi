//! Obstruction test for a battle actor moving in one of the four directions
//! (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// Direction table in the scratchpad (OBSTRUCTION_DIRECTIONS).
fn dirEntry(direction: c_int) *align(1) v.D_800F16EC_t {
    const table: [*]align(1) v.D_800F16EC_t = @ptrFromInt(0x1F8003EC);
    return psx.at(table, direction);
}

/// Per-state word in the scratchpad table at 0x1F80037C (OBSTRUCTION_STATES).
fn scratchState(id: i32) *align(1) v.obstructionState {
    // 4 * id wraps in int, then sign-extends to the target's address width.
    const off: isize = @as(c_int, 4) *% id;
    const addr: usize = @as(usize, 0x1F80037C) +% @as(usize, @bitCast(off));
    const word = @as(*const u32, @ptrFromInt(addr)).*;
    return @ptrFromInt(@as(usize, word));
}

/// True when the point (dx, dz) lies behind the direction's travel.
fn behindDirection(direction: c_int, dx: c_int, dz: c_int) bool {
    const d = dirEntry(direction);
    const vx: c_int = @as(c_int, psx.getBits(d, "dx"));
    const vz: c_int = @as(c_int, psx.getBits(d, "dz"));
    return (vx > 0 and dx < 0) or (vx < 0 and dx > 0) or (vz > 0 and dz < 0) or (vz < 0 and dz > 0);
}

/// C ABS() cast to unsigned: -INT_MIN stays INT_MIN.
fn absU(x: c_int) c_uint {
    return psx.conv(c_uint, if (x >= 0) x else -%x);
}

/// cache->tested |= 1 << direction.
fn markTested(cache: *v.obstructionCache, direction: c_int) void {
    // Shift count is the low 5 bits (x86 masks it).
    const sh: u5 = psx.conv(u5, direction);
    cache.tested = psx.conv(u8, @as(c_int, cache.tested) | (@as(c_int, 1) << sh));
}

/// orig: func_800E02B4 (src/BATTLE/BATTLE.PRG/771A8.c)
/// Tests whether a move of `state` in `direction` (`ahead` steps) is blocked by a point or an actor, caching the answer per direction.
pub fn func_800E02B4(state: ?*v.obstructionState, direction: c_int, ahead: c_int) callconv(.c) c_int {
    const cache: *v.obstructionCache = @ptrFromInt(0x1F8003D0);
    // Shift count is the low 5 bits (x86 masks it); direction is not range-checked by the C.
    const shift: u5 = psx.conv(u5, direction);
    if (((@as(c_int, cache.tested) >> shift) & 1) != 0)
        return @as(c_int, psx.at(&cache.result, direction).*);

    const st = state.?;
    const dir = dirEntry(direction);
    const dvx: c_int = @as(c_int, psx.getBits(dir, "dx"));
    const dvz: c_int = @as(c_int, psx.getBits(dir, "dz"));
    const x: c_int = @as(c_int, st.position.x) +% ahead *% (@as(c_int, st.step) *% dvx);
    const y: c_int = @as(c_int, st.position.y);
    const z: c_int = @as(c_int, st.position.z) +% ahead *% (@as(c_int, st.step) *% dvz);

    var points: [4]v.obstructionVector align(4) = undefined;
    var i: c_int = c.func_8008D2C0(@ptrCast(&points));
    i = i -% 1;
    while (i != -1) : (i -%= 1) {
        const p = psx.at(&points, i);
        const dx: c_int = @as(c_int, p.x) -% x;
        const dy: c_int = @as(c_int, p.y) -% y;
        const dz: c_int = @as(c_int, p.z) -% z;
        if (ahead != 0 and behindDirection(direction, dx, dz)) continue;

        const radius: c_uint = psx.conv(c_uint, @as(c_int, st.radius) +% 96);
        const height: c_uint = psx.conv(c_uint, (@as(c_int, st.height) +% 32) >> 1);
        if (absU(dx) <= radius and absU(dz) <= radius and absU(dy) <= height) {
            markTested(cache, direction);
            psx.at(&cache.result, direction).* = 1;
            return 1;
        }
    }

    var actor: ?*v.vs_battle_actor = v.g.vs_battle_actors()[0];
    while (actor) |a| : (actor = psx.get(a.next)) {
        const other = scratchState(a.id);
        const model = psx.get(other.model);
        if (other.id == st.id or other.id == 0) continue;

        const m = model.?;
        const dx: c_int = @as(c_int, m.position.x) -% x;
        const dy: c_int = @as(c_int, m.position.y) -% y;
        const dz: c_int = @as(c_int, m.position.z) -% z;
        if (ahead != 0 and behindDirection(direction, dx, dz)) continue;

        const radius: c_uint = psx.conv(c_uint, @as(c_int, other.radius) +% @as(c_int, st.radius) +% @as(c_int, other.step));
        const height: c_uint = if (other.id == 0)
            @as(c_uint, 0xFFFF_FFFF)
        else
            psx.conv(c_uint, (@as(c_int, st.height) +% @as(c_int, other.height)) >> 1);
        if (absU(dx) <= radius and absU(dz) <= radius and absU(dy) <= height) {
            markTested(cache, direction);
            psx.at(&cache.result, direction).* = psx.conv(u8, a.id +% 1);
            return a.id +% 1;
        }
    }

    markTested(cache, direction);
    psx.at(&cache.result, direction).* = 0;
    return 0;
}
