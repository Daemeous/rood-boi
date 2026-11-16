//! Actor movement/action commands (D2: 766FC.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const psx = @import("common").psx;
const pad = @import("common").pad;
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");

fn u32At(base: [*]u8, offset: usize) *align(1) u32 {
    return @ptrCast(base + offset);
}
fn u16At(base: [*]u8, offset: usize) *align(1) u16 {
    return @ptrCast(base + offset);
}
fn i16At(base: [*]u8, offset: usize) *align(1) i16 {
    return @ptrCast(base + offset);
}
fn i32At(base: [*]u8, offset: usize) *align(1) c_int {
    return @ptrCast(base + offset);
}
/// The command block is the actor's own words: unkC, unk10 and unk14 (as words, as halves, as signed).
fn w0C(a: *ai.vs_battle_actor) *u32 { return @ptrCast(&a.unkC); }
fn w10(a: *ai.vs_battle_actor) *u32 { return @ptrCast(&a.unk10); }
fn w14(a: *ai.vs_battle_actor) *u32 { return @ptrCast(&a.unk14); }
fn h0C(a: *ai.vs_battle_actor) *u16 { return @ptrCast(&a.unkC); }
fn h10(a: *ai.vs_battle_actor) *u16 { return @ptrCast(&a.unk10); }
fn h14(a: *ai.vs_battle_actor) *u16 { return @ptrCast(&a.unk14); }
fn s0C(a: *ai.vs_battle_actor) *c_int { return &a.unkC; }
fn s10(a: *ai.vs_battle_actor) *c_int { return &a.unk10.s32; }
/// A byte of the scratchpad at `offset`.
fn scratchByte(offset: c_int) *u8 {
    return @ptrFromInt(pad.addr(@intCast(offset)));
}

/// orig: gait_speed (pc/src/battle_movement.c)
/// Speed selection from the actor's gait word 0x224: running (byte 1) uses
/// mask 0x84/0x8400 (speed from byte 2/3), walking uses 0x42 and `walk_high`.
/// Returns 0 with *command set, or -1 to stand.
fn gaitSpeed(state: *ai.vs_battle_aiState, walk_high: c_uint, speed: *c_uint, command: *c_int) c_int {
    const gait = @as(*align(1) u32, @ptrCast(&state.field224)).*;
    if (state.flags0.b[1] != 0) {
        if ((gait & 0x84) != 0) {
            speed.* = (gait >> 16) & 0xff;
            command.* = 10;
            return 0;
        }
        if ((gait & 0x8400) != 0) {
            speed.* = (gait >> 24) & 0xff;
            command.* = 10;
            return 0;
        }
    }
    if ((gait & 0x42) != 0) {
        speed.* = (gait >> 16) & 0xff;
        command.* = 9;
        return 0;
    }
    if ((gait & walk_high) != 0) {
        speed.* = (gait >> 24) & 0xff;
        command.* = 9;
        return 0;
    }
    return -1;
}

/// orig: move_toward (pc/src/battle_movement.c)
/// The shared move: faces `angle_in`, picks the gait and steps; if a corner
/// of the actor's square (half-size 0x92) would enter a blocked neighbour,
/// slides to the tile edge instead (command 0x0D).
fn moveToward(state: *ai.vs_battle_aiState, actor: *ai.vs_battle_actor, angle_in: c_int) void {
    const size: c_int = state.radius;
    const tile = state.tile.raw;
    const tile_x: c_int = @intCast(tile & 0xff); // a byte
    const tile_z: c_int = @intCast((tile >> 16) & 0xff);
    const angle = angle_in & 0xfff;
    h0C(actor).* = @intCast(angle); // masked to 12 bits
    const turned = (0xc00 -% angle) & 0xfff;
    const x: c_int = state.position.xyz[0];
    var speed: c_uint = undefined;
    var kind: c_int = undefined;
    if (gaitSpeed(state, 0x420000, &speed, &kind) < 0) {
        actor.unk9 = 2;
        return;
    }
    actor.unk9 = @intCast(kind); // 9 or 10
    const step_x = c.rcos(turned) *% @as(c_int, @bitCast(speed));
    const step_z = c.rsin(turned) *% @as(c_int, @bitCast(speed));
    const z: c_int = state.position.xyz[2];
    const new_x = x +% (step_x >> 12);
    const new_z = z +% (step_z >> 12);
    const moved: c_uint = @as(c_uint, @bitCast(new_x)) | (@as(c_uint, @bitCast(new_z)) << 16);
    const lo_x = (((new_x -% size) >> 7) -% tile_x) +% 1;
    const lo_z = (((new_z -% size) >> 7) -% tile_z) +% 1;
    const hi_x = (((new_x +% size) >> 7) -% tile_x) +% 1;
    const hi_z = (((new_z +% size) >> 7) -% tile_z) +% 1;
    const corners = [4][2]c_int{ .{ lo_x, lo_z }, .{ lo_x, hi_z }, .{ hi_x, lo_z }, .{ hi_x, hi_z } };
    var blocked: c_int = 0;
    var i: usize = 0;
    while (i < 4 and blocked == 0) : (i += 1) { // i < 4: the corner count
        const dir: c_int = @as(*const i8, @ptrCast(scratchByte(0x3e0 +% corners[i][1] *% 4 +% corners[i][0]))).*;
        if (dir < 0) continue;
        if (@as(*const u32, @ptrFromInt(pad.addr(0x3F4))).* != 0) {
            const floors: usize = @as(*const u32, @ptrFromInt(pad.addr(0x3BC))).*;
            const bitmap: usize = @as(*const u32, @ptrFromInt(pad.addr(0x3C4))).*;
            const here_cell: *align(1) const u16 = @ptrFromInt(floors +% @as(usize, @bitCast(@as(isize, tile_z *% 64) +% @as(isize, tile_x *% 2))));
            const here: c_int = (@as(c_int, here_cell.*) >> 10) & 7;
            const there_z = (tile_z +% corners[i][1]) -% 1;
            const there_x = (tile_x +% corners[i][0]) -% 1;
            const there_cell: *align(1) const u16 = @ptrFromInt(floors +% @as(usize, @bitCast(@as(isize, there_z *% 64) +% @as(isize, there_x *% 2))));
            const there: c_int = (@as(c_int, there_cell.*) >> 10) & 7;
            const index: isize = @as(c_int, scratchByte(0x3dc).*) *% tile_z +% tile_x;
            const byte: *const u8 = @ptrFromInt(bitmap +% @as(usize, @bitCast(index)));
            // dir is 0..127 here; the C shifts by it (x86 masks the amount to 5 bits).
            blocked = ((@as(c_int, byte.*) >> @truncate(@as(c_uint, @bitCast(dir)))) & 1) | @intFromBool(here < there);
        } else {
            blocked = c.vs_battle_aiIsAdjacentTileStepBlocked(tile, dir);
        }
    }
    if (blocked == 0) return;
    const centre_x = tile_x *% 128 +% 64;
    const centre_z = tile_z *% 128 +% 64;
    const slide_x = (if (@as(c_int, @bitCast(moved & 0xffff)) < centre_x) centre_x -% 0x40 +% size else centre_x +% 0x3f -% size) -% x;
    const slide_z = (if (@as(c_int, @bitCast(moved >> 16)) < centre_z) centre_z -% 0x40 +% size else centre_z +% 0x3f -% size) -% z;
    if ((slide_x | slide_z) == 0) return;
    const packed_tile: c_uint = (@as(c_uint, @bitCast(tile_z)) << 16) | @as(c_uint, @bitCast(tile_x));
    if ((state.field9C & 0xff00ff) == packed_tile) return;
    state.field9C = @bitCast(packed_tile);
    actor.unk9 = 0xd;
    s0C(actor).* = slide_x << 12;
    s10(actor).* = slide_z << 12;
}

/// orig: walk_to_tile (pc/src/battle_movement.c)
/// Entry 13: walks to the stored tile 0x170.
fn walkToTile(state: *ai.vs_battle_aiState, actor: *ai.vs_battle_actor) void {
    const tile = state.next.movementPosition.raw;
    actor.unk9 = 0x17;
    w0C(actor).* = tile;
    const x: c_int = @bitCast((tile & 0xff) *% 128 +% 64);
    const z: c_int = @as(c_int, @bitCast((tile << 8) >> 17)) +% 64;
    state.unk178 = psx.conv(u16, z);
    state.unk174 = (@as(c_uint, @bitCast(c.vs_field_groundHeightAtWorldPosition(x, z))) << 16) | @as(c_uint, @bitCast(x));
    const own_x: c_int = state.position.xyz[0];
    const own_z: c_int = state.position.xyz[2];
    const heading = c.ratan2(own_x -% x, own_z -% z);
    s10(actor).* = heading;
    c.vs_battle_aiReserveAiDestinationTile(@intCast(tile & 0xff), @intCast((tile >> 16) & 0xff)); // bytes
}

/// orig: vs_battle_aiIssueActorCommand (pc/src/battle_movement.c)
/// Issues movement/action command `action` to the actor's command block
/// (0x54): records it at scratchpad 0x3C8, resets the block (byte 8 = 0x80,
/// byte 10 = facing target) and fills it per action (jtbl_800F16F8, 18
/// entries). Byte 9 receives the command code.
pub fn issueCommand(state: *ai.vs_battle_aiState, action: c_int) callconv(.c) void {
    const actor: *ai.vs_battle_actor = @ptrCast(state.actor);
    @as(*u32, @ptrFromInt(pad.addr(0x3C8))).* = @bitCast(action);
    actor.unk8 = 0x80;
    actor.unkA = state.movementTargetId;
    switch (action) {
        0 => actor.unk9 = 2,
        1 => actor.unk9 = 1,
        2 => actor.unk9 = 0x14,
        3 => {
            actor.unk9 = 3;
            w0C(actor).* = (@as(c_uint, state.delay) << 16) | (@as(u16, @bitCast(state.heading)) & 0xfff);
        },
        4 => {
            const model: [*]u8 = @ptrCast(state.object.actorObject);
            const target: c_int = state.angle18A & 0xfff;
            const heading: c_int = i16At(model, 0x26).*;
            const turn: c_int = i16At(model, 0x14).*;
            const offset: c_int = (target -% (turn +% heading)) & 0xfff;
            var sign: c_int = undefined;
            var limit_ok: c_int = undefined;
            if (offset < 0x800) {
                limit_ok = @intFromBool(turn < 0x320);
                sign = 1;
            } else {
                limit_ok = @intFromBool(-0x320 < turn);
                sign = -1;
            }
            const code: u8 = if ((@as(c_int, state.field2A) & limit_ok) != 0) 7 else 3;
            const rate: c_uint = state.timer13A;
            actor.unk9 = code;
            var value: c_int = undefined;
            if ((0x1000 -% offset) < @as(c_int, @bitCast(rate)) or offset < @as(c_int, @bitCast(rate)) or offset < 1) {
                actor.unk9 = 4;
                value = target;
            } else {
                value = sign *% @as(c_int, @bitCast(rate)) +% (if (code == 3) heading else turn);
            }
            w0C(actor).* = (@as(c_uint, @bitCast(value)) & 0xffff) | (rate << 16);
        },
        5 => moveToward(state, actor, @as(c_int, state.heading) << 9),
        6 => {
            const tile = state.tile.raw;
            const target = state.destination168.destination;
            moveToward(state, actor, c.ratan2(
                @as(c_int, @intCast(tile & 0xff)) - @as(c_int, @intCast(target & 0xff)), // bytes
                @bitCast(((tile >> 8) << 24) -% ((target >> 8) << 24)),
            ));
        },
        7 => moveToward(state, actor, state.heading),
        8 => {
            actor.unk9 = 0x13;
            w0C(actor).* = @bitCast(state.moveX);
            w14(actor).* = @bitCast(state.moveY);
            w10(actor).* = @bitCast(state.moveZ);
        },
        9, 10 => {
            actor.unk9 = if (action == 9) 0x10 else 0x11;
            w0C(actor).* = @bitCast((state.moveY << 16) | (state.moveX & 0xffff));
            h10(actor).* = @truncate(@as(u32, @bitCast(state.moveZ)));
        },
        11 => {
            const tile = state.next.movementPosition.raw;
            w0C(actor).* = tile;
            actor.unk9 = 0x15;
            c.vs_battle_aiReserveAiDestinationTile(@intCast(tile & 0xff), @intCast((tile >> 16) & 0xff)); // bytes
        },
        12 => {
            const tile = state.next.movementPosition.raw;
            const lift: c_int = state.heightBE;
            actor.unk9 = 0x16;
            const x: c_int = @as(c_int, @intCast(tile & 0xff)) << 7; // a byte
            const z: c_int = @as(c_int, @bitCast((tile << 8) >> 17)) +% 0x40;
            h10(actor).* = psx.conv(u16, z);
            const height = c.vs_field_groundHeightAtWorldPosition(x, z);
            w0C(actor).* = (@as(c_uint, @bitCast(height +% lift)) << 16) | @as(c_uint, @bitCast(x +% 0x40));
        },
        13 => walkToTile(state, actor),
        14 => {
            const here = c.vs_field_groundHeightAtPackedTile(state.tile.raw);
            const tile = state.next.movementPosition.raw;
            var rise = c.vs_field_groundHeightAtPackedTile(tile) -% here;
            const flags: c_uint = state.flags;
            var code: u8 = undefined;
            if (rise < 0) rise = -%rise;
            if (rise < @as(c_int, state.fB0)) {
                code = 0xb;
            } else if ((flags & 8) != 0) {
                walkToTile(state, actor);
                return;
            } else if ((flags & 0x30) != 0) {
                code = 0x12;
            } else if ((flags & 0xc0) != 0) {
                code = 0xe;
            } else {
                code = 0xb;
            }
            actor.unk9 = code;
            w0C(actor).* = tile;
            const tile_x: c_int = @intCast(tile & 0xff); // a byte
            const tile_z: c_int = @intCast((tile >> 16) & 0xff);
            const x = tile_x *% 128 +% 64;
            const z = tile_z *% 128 +% 64;
            state.unk178 = psx.conv(u16, z);
            state.unk174 = (@as(c_uint, @bitCast(c.vs_field_groundHeightAtWorldPosition(x, z))) << 16) | @as(c_uint, @bitCast(x));
            var speed: c_uint = undefined;
            var kind: c_int = undefined;
            if (gaitSpeed(state, 0x4200, &speed, &kind) < 0) {
                actor.unk9 = 2;
                return;
            }
            w10(actor).* = speed;
            if (actor.id != 0 and (tile & 0xff00) == 0) c.vs_battle_aiReserveAiDestinationTile(tile_x, tile_z);
        },
        15 => {
            const candidate: [*]u32 = @ptrCast(state.candidate);
            const slot = candidate[1];
            const target = candidate[3];
            const row: c_int = @intCast(slot & 7);
            const column: c_int = @intCast((slot >> 3) & 3);
            w0C(actor).* = (@as(c_uint, @bitCast(column)) << 16) | @as(c_uint, @bitCast(row));
            const slot_flags = psx.fieldAt(u32, &state.configs, @as(isize, @intCast(row)) * 32 + @as(isize, @intCast(column)) * 8).*; // row 0..7, column 0..3
            if (((@as(c_uint, 1) << @intCast((slot_flags >> 3) & 3)) & 6) == 0) {
                const model: [*]u8 = @ptrCast(v.g.vs_actors_actorRecords()[target & 0xf].?);
                w10(actor).* = u32At(model, 0x1c).* -% (((target >> 7) & 0xfff) << 16);
                h14(actor).* = u16At(model, 0x20).*;
            } else {
                w10(actor).* = (((target >> 4) & 7) << 16) | (target & 0xf);
            }
            actor.unk9 = 0x18;
        },
        16 => {
            actor.unk9 = 6;
            w0C(actor).* = @as(u16, @bitCast(state.heading)) | (@as(c_uint, state.timer13A) << 16);
        },
        17 => actor.unk9 = 8,
        else => c.pc_unimplemented("actor command beyond jtbl_800F16F8"),
    }
}
