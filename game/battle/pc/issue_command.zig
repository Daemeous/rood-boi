//! Actor movement/action commands (D2: 766FC.s; oracle pc/src/battle_movement.c).
const psx = @import("common").psx;
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
/// A byte of the scratchpad at 0x1F800000 + offset (the C's int sum, as a 32-bit guest address).
fn scratchByte(offset: c_int) *u8 {
    return @ptrFromInt(@as(usize, @as(u32, @bitCast(@as(c_int, 0x1f800000) +% offset))));
}

/// orig: gait_speed (pc/src/battle_movement.c)
/// Speed selection from the actor's gait word 0x224: running (byte 1) uses
/// mask 0x84/0x8400 (speed from byte 2/3), walking uses 0x42 and `walk_high`.
/// Returns 0 with *command set, or -1 to stand.
fn gaitSpeed(base: [*]u8, walk_high: c_uint, speed: *c_uint, command: *c_int) c_int {
    const gait = u32At(base, 0x224).*;
    if (base[1] != 0) {
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
fn moveToward(base: [*]u8, command: [*]u8, angle_in: c_int) void {
    const size: c_int = u16At(base, 0x92).*;
    const tile = u32At(base, 0x34).*;
    const tile_x: c_int = @intCast(tile & 0xff); // a byte
    const tile_z: c_int = @intCast((tile >> 16) & 0xff);
    const angle = angle_in & 0xfff;
    u16At(command, 0xc).* = @intCast(angle); // masked to 12 bits
    const turned = (0xc00 -% angle) & 0xfff;
    const x: c_int = u16At(base, 0x4c).*;
    var speed: c_uint = undefined;
    var kind: c_int = undefined;
    if (gaitSpeed(base, 0x420000, &speed, &kind) < 0) {
        command[9] = 2;
        return;
    }
    command[9] = @intCast(kind); // 9 or 10
    const step_x = c.rcos(turned) *% @as(c_int, @bitCast(speed));
    const step_z = c.rsin(turned) *% @as(c_int, @bitCast(speed));
    const z: c_int = u16At(base, 0x50).*;
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
        if (@as(*const u32, @ptrFromInt(0x1f8003f4)).* != 0) {
            const floors: usize = @as(*const u32, @ptrFromInt(0x1f8003bc)).*;
            const bitmap: usize = @as(*const u32, @ptrFromInt(0x1f8003c4)).*;
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
            blocked = c.func_800E4690(tile, dir);
        }
    }
    if (blocked == 0) return;
    const centre_x = tile_x *% 128 +% 64;
    const centre_z = tile_z *% 128 +% 64;
    const slide_x = (if (@as(c_int, @bitCast(moved & 0xffff)) < centre_x) centre_x -% 0x40 +% size else centre_x +% 0x3f -% size) -% x;
    const slide_z = (if (@as(c_int, @bitCast(moved >> 16)) < centre_z) centre_z -% 0x40 +% size else centre_z +% 0x3f -% size) -% z;
    if ((slide_x | slide_z) == 0) return;
    const packed_tile: c_uint = (@as(c_uint, @bitCast(tile_z)) << 16) | @as(c_uint, @bitCast(tile_x));
    if ((u32At(base, 0x9c).* & 0xff00ff) == packed_tile) return;
    u32At(base, 0x9c).* = packed_tile;
    command[9] = 0xd;
    i32At(command, 0xc).* = slide_x << 12;
    i32At(command, 0x10).* = slide_z << 12;
}

/// orig: walk_to_tile (pc/src/battle_movement.c)
/// Entry 13: walks to the stored tile 0x170.
fn walkToTile(base: [*]u8, command: [*]u8) void {
    const tile = u32At(base, 0x170).*;
    command[9] = 0x17;
    u32At(command, 0xc).* = tile;
    const x: c_int = @bitCast((tile & 0xff) *% 128 +% 64);
    const z: c_int = @as(c_int, @bitCast((tile << 8) >> 17)) +% 64;
    u16At(base, 0x178).* = psx.conv(u16, z);
    u32At(base, 0x174).* = (@as(c_uint, @bitCast(c.func_800E42D4(x, z))) << 16) | @as(c_uint, @bitCast(x));
    const own_x: c_int = u16At(base, 0x4c).*;
    const own_z: c_int = u16At(base, 0x50).*;
    const heading = c.ratan2(own_x -% x, own_z -% z);
    i32At(command, 0x10).* = heading;
    c.func_800E4C1C(@intCast(tile & 0xff), @intCast((tile >> 16) & 0xff)); // bytes
}

/// orig: func_800DEEFC (pc/src/battle_movement.c)
/// Issues movement/action command `action` to the actor's command block
/// (0x54): records it at scratchpad 0x3C8, resets the block (byte 8 = 0x80,
/// byte 10 = facing target) and fills it per action (jtbl_800F16F8, 18
/// entries). Byte 9 receives the command code.
pub fn issueCommand(state: [*]u8, action: c_int) callconv(.c) void {
    const command: [*]u8 = @ptrFromInt(u32At(state, 0x54).*);
    @as(*u32, @ptrFromInt(0x1f8003c8)).* = @bitCast(action);
    command[8] = 0x80;
    command[0xa] = state[0x15c];
    switch (action) {
        0 => command[9] = 2,
        1 => command[9] = 1,
        2 => command[9] = 0x14,
        3 => {
            command[9] = 3;
            u32At(command, 0xc).* = (@as(c_uint, u16At(state, 0xba).*) << 16) | (u16At(state, 0x188).* & 0xfff);
        },
        4 => {
            const model: [*]u8 = @ptrFromInt(u32At(state, 0x58).*);
            const target: c_int = u16At(state, 0x18a).* & 0xfff;
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
            const code: u8 = if ((@as(c_int, state[0x2a]) & limit_ok) != 0) 7 else 3;
            const rate: c_uint = u16At(state, 0x13a).*;
            command[9] = code;
            var value: c_int = undefined;
            if ((0x1000 -% offset) < @as(c_int, @bitCast(rate)) or offset < @as(c_int, @bitCast(rate)) or offset < 1) {
                command[9] = 4;
                value = target;
            } else {
                value = sign *% @as(c_int, @bitCast(rate)) +% (if (code == 3) heading else turn);
            }
            u32At(command, 0xc).* = (@as(c_uint, @bitCast(value)) & 0xffff) | (rate << 16);
        },
        5 => moveToward(state, command, @as(c_int, i16At(state, 0x188).*) << 9),
        6 => {
            const tile = u32At(state, 0x34).*;
            const target = u32At(state, 0x168).*;
            moveToward(state, command, c.ratan2(
                @as(c_int, @intCast(tile & 0xff)) - @as(c_int, @intCast(target & 0xff)), // bytes
                @bitCast(((tile >> 8) << 24) -% ((target >> 8) << 24)),
            ));
        },
        7 => moveToward(state, command, i16At(state, 0x188).*),
        8 => {
            command[9] = 0x13;
            u32At(command, 0xc).* = u32At(state, 0x17c).*;
            u32At(command, 0x14).* = u32At(state, 0x184).*;
            u32At(command, 0x10).* = u32At(state, 0x180).*;
        },
        9, 10 => {
            command[9] = if (action == 9) 0x10 else 0x11;
            u32At(command, 0xc).* = (u32At(state, 0x184).* << 16) | (u32At(state, 0x17c).* & 0xffff);
            u16At(command, 0x10).* = @truncate(u32At(state, 0x180).*);
        },
        11 => {
            const tile = u32At(state, 0x170).*;
            u32At(command, 0xc).* = tile;
            command[9] = 0x15;
            c.func_800E4C1C(@intCast(tile & 0xff), @intCast((tile >> 16) & 0xff)); // bytes
        },
        12 => {
            const tile = u32At(state, 0x170).*;
            const lift: c_int = i16At(state, 0xbe).*;
            command[9] = 0x16;
            const x: c_int = @as(c_int, @intCast(tile & 0xff)) << 7; // a byte
            const z: c_int = @as(c_int, @bitCast((tile << 8) >> 17)) +% 0x40;
            u16At(command, 0x10).* = psx.conv(u16, z);
            const height = c.func_800E42D4(x, z);
            u32At(command, 0xc).* = (@as(c_uint, @bitCast(height +% lift)) << 16) | @as(c_uint, @bitCast(x +% 0x40));
        },
        13 => walkToTile(state, command),
        14 => {
            const here = c.func_800E42E0(u32At(state, 0x34).*);
            const tile = u32At(state, 0x170).*;
            var rise = c.func_800E42E0(tile) -% here;
            const flags: c_uint = u16At(state, 0x15e).*;
            var code: u8 = undefined;
            if (rise < 0) rise = -%rise;
            if (rise < @as(c_int, u16At(state, 0xb0).*)) {
                code = 0xb;
            } else if ((flags & 8) != 0) {
                walkToTile(state, command);
                return;
            } else if ((flags & 0x30) != 0) {
                code = 0x12;
            } else if ((flags & 0xc0) != 0) {
                code = 0xe;
            } else {
                code = 0xb;
            }
            command[9] = code;
            u32At(command, 0xc).* = tile;
            const tile_x: c_int = @intCast(tile & 0xff); // a byte
            const tile_z: c_int = @intCast((tile >> 16) & 0xff);
            const x = tile_x *% 128 +% 64;
            const z = tile_z *% 128 +% 64;
            u16At(state, 0x178).* = psx.conv(u16, z);
            u32At(state, 0x174).* = (@as(c_uint, @bitCast(c.func_800E42D4(x, z))) << 16) | @as(c_uint, @bitCast(x));
            var speed: c_uint = undefined;
            var kind: c_int = undefined;
            if (gaitSpeed(state, 0x4200, &speed, &kind) < 0) {
                command[9] = 2;
                return;
            }
            u32At(command, 0x10).* = speed;
            if (u32At(command, 4).* != 0 and (tile & 0xff00) == 0) c.func_800E4C1C(tile_x, tile_z);
        },
        15 => {
            const candidate: [*]u32 = @ptrFromInt(u32At(state, 0x22c).*);
            const slot = candidate[1];
            const target = candidate[3];
            const row: c_int = @intCast(slot & 7);
            const column: c_int = @intCast((slot >> 3) & 3);
            u32At(command, 0xc).* = (@as(c_uint, @bitCast(column)) << 16) | @as(c_uint, @bitCast(row));
            const slot_flags = u32At(state, 0x230 +% @as(usize, @intCast(row)) * 32 +% @as(usize, @intCast(column)) * 8).*; // row 0..7, column 0..3
            if (((@as(c_uint, 1) << @intCast((slot_flags >> 3) & 3)) & 6) == 0) {
                const model: [*]u8 = @ptrCast(v.g.D_800F4538()[target & 0xf].?);
                u32At(command, 0x10).* = u32At(model, 0x1c).* -% (((target >> 7) & 0xfff) << 16);
                u16At(command, 0x14).* = u16At(model, 0x20).*;
            } else {
                u32At(command, 0x10).* = (((target >> 4) & 7) << 16) | (target & 0xf);
            }
            command[9] = 0x18;
        },
        16 => {
            command[9] = 6;
            u32At(command, 0xc).* = u16At(state, 0x188).* | (@as(c_uint, u16At(state, 0x13a).*) << 16);
        },
        17 => command[9] = 8,
        else => c.pc_unimplemented("actor command beyond jtbl_800F16F8"),
    }
}
