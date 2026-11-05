//! The actors' queued commands (pc/src/actor_commands.inc, compiled into src/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// `*(u_int*)&actor->unk8`: the command header word (state, command, target, effect bits 24..25, camera bits 26..27).
fn header(actor: *v.vs_battle_actor) c_uint {
    const word: *align(1) const c_uint = @ptrCast(&actor.unk8);
    return word.*;
}

/// The actor's `unkC` as bytes.
fn bytesOfC(actor: *v.vs_battle_actor) *[4]u8 {
    return @ptrCast(&actor.unkC);
}

/// The actor's `unkC` as shorts.
fn shortsOfC(actor: *v.vs_battle_actor) *[2]i16 {
    return @ptrCast(&actor.unkC);
}

/// `ratan2(target - position)` plus a half turn: the angle toward the target actor.
fn angleToTarget(target: *v.func_8006EBF8_t, position: *v.func_8006EBF8_t) c_int {
    return c.ratan2(@as(c_int, target.unk0.unk4.vx) -% position.unk0.unk4.vx, @as(c_int, target.unk0.unk4.vz) -% position.unk0.unk4.vz) +% 0x800;
}

/// orig: func_80075554 (src/BATTLE/BATTLE.PRG/146C.c)
/// Runs the queued command of each actor: a command in progress (state 0x40) ends when its movement or animation is done; a new one (state 0x80) starts the camera and effect bits, then moves, turns or animates the actor as its command says. A nonzero result of func_800751B8 ends the pass with it.
pub fn func_80075554() callconv(.c) c_int {
    // uninitialised in the original where a command leaves a field unset: zeroed
    var vector = std.mem.zeroes(v.SVECTOR);
    var position = std.mem.zeroes(v.func_8006EBF8_t);
    var target = std.mem.zeroes(v.func_8006EBF8_t);
    var state: c_int = undefined;
    var angle: c_int = undefined;
    var speed: c_int = undefined;

    var i: c_int = 0;
    while (i < 16) : (i +%= 1) {
        const actor = v.g.vs_battle_actors()[@intCast(i)] orelse continue; // i < 16
        if ((c.func_800A0BE0(i) & 0x1000000) != 0) continue;
        if (actor.allegiance != 4 and actor.allegiance != 2 and v.g.D_800F196C().* != 3) continue;

        if (actor.unk8 == 0x40) {
            state = actor.unk8;
            switch (actor.unk9) {
                9, 10, 11, 12, 14, 15, 18 => {
                    if ((c.func_800A0BE0(i) & 0x4000) == 0) state = 0;
                },
                13 => {
                    c.func_800753F8(@ptrCast(actor), psx.cptr(c.SVECTOR, &vector));
                    c.func_800B6500(i, psx.cptr(c.SVECTOR, &vector));
                    const busy = @as(c_uint, @bitCast(actor.unkC | actor.unk10.s32)) | (c.func_800A0BE0(i) & 0x4000);
                    state = @as(c_int, @intFromBool(busy != 0)) << 6;
                },
                23 => {
                    if ((c.func_800A0BE0(i) & 0x12000000) == 0) state = 0;
                },
                20, 21, 22 => {
                    if ((c.func_800A0BE0(i) & 0x4000000) == 0) state = 0;
                },
                else => c.vs_main_nop9(0xC1, 0),
            }
            actor.unk8 = psx.conv(u8, state);
            if (v.g.D_800F196C().* == 3) c.func_800A4E68(i);
            continue;
        }

        if (actor.unk8 != 0x80) continue;

        _ = c.func_800A1108(i, &position);
        const flags = header(actor);
        if ((flags & 0x3000000) != 0) {
            c.func_800AE4FC(@ptrCast(psx.get(actor.unk44)), psx.conv(c_int, (flags >> 24) & 3));
        }
        switch ((header(actor) >> 26) & 3) {
            1 => c.func_8006D028(i),
            2 => c.func_8006D064(),
            else => {},
        }
        actor.unkB &= 0xF3; // camera = 0

        switch (actor.unk9) {
            2 => {
                if (actor.unkA >= 16) {
                    c.func_8009E614(i);
                } else {
                    c.func_8009E5C4(i);
                }
                state = 0;
            },
            3 => {
                _ = c.func_8009FC60(i, -4, null, 0);
                c.func_800B6634(i, shortsOfC(actor)[0]);
                state = 0;
            },
            4 => {
                vector.vx = 0;
                vector.vy = shortsOfC(actor)[0];
                _ = c.func_8009FC60(i, -3, psx.cptr(c.SVECTOR, &vector), -1);
                state = 0;
                c.func_800B668C(i, shortsOfC(actor)[0], shortsOfC(actor)[1]);
            },
            15 => {
                _ = c.func_8009FC60(i, -2, null, -1);
                if (actor.unkA >= 16) {
                    c.func_800B64CC(i, shortsOfC(actor)[0], @as(c_int, shortsOfC(actor)[1]) << 12, shortsOfC(actor)[0]);
                } else {
                    _ = c.func_800A1108(actor.unkA, &target);
                    c.func_800B64CC(i, shortsOfC(actor)[0], @as(c_int, shortsOfC(actor)[1]) << 12, angleToTarget(&target, &position));
                }
                state = @as(c_int, @intFromBool((c.func_800A0BE0(i) & 0x4000) != 0)) << 6;
            },
            9 => {
                _ = c.func_8009FC60(i, -2, null, -1);
                const stats = psx.get(actor.stats).?;
                if (stats.unk32 == 2) {
                    speed = @as(c_int, stats.unk33) << 12;
                } else if (stats.unk32 == 0x40) {
                    speed = @as(c_int, stats.unk33) << 12;
                } else {
                    speed = @as(c_int, stats.unk31) << 12;
                }
                if (actor.unkA < 16) {
                    _ = c.func_800A1108(actor.unkA, &target);
                    c.func_800B64CC(i, shortsOfC(actor)[0], speed, angleToTarget(&target, &position));
                } else {
                    c.func_800B64CC(i, shortsOfC(actor)[0], speed, shortsOfC(actor)[0]);
                }
                state = @as(c_int, @intFromBool((c.func_800A0BE0(i) & 0x4000) != 0)) << 6;
            },
            10 => {
                _ = c.func_8009FC60(i, -2, null, -1);
                const stats = psx.get(actor.stats).?;
                if (stats.unk32 == 4) {
                    speed = @as(c_int, stats.unk33) << 12;
                } else if (stats.unk32 == 0x80) {
                    speed = @as(c_int, stats.unk33) << 12;
                } else {
                    speed = @as(c_int, stats.unk31) << 12;
                }
                c.func_800B64CC(i, shortsOfC(actor)[0], speed, shortsOfC(actor)[0]);
                state = @as(c_int, @intFromBool((c.func_800A0BE0(i) & 0x4000) != 0)) << 6;
            },
            11, 12, 14, 18 => {
                _ = c.func_8009FC60(i, -2, null, -1);
                target.unk0.unk4.vx = psx.conv(i16, (@as(c_int, bytesOfC(actor)[0]) << 7) +% 0x40);
                target.unk0.unk4.vz = psx.conv(i16, (@as(c_int, bytesOfC(actor)[2]) << 7) +% 0x40);
                angle = angleToTarget(&target, &position);
                switch (actor.unk9) {
                    11, 12 => c.func_800B63AC(i, &actor.unkC, actor.unk10.s32, angle),
                    18 => c.func_800B6438(i, &actor.unkC, actor.unk10.s32, angle),
                    14 => c.func_800B6404(i, &actor.unkC, actor.unk10.s32, angle),
                    else => {},
                }
                state = 0x40;
            },
            13 => {
                c.func_800753F8(@ptrCast(actor), psx.cptr(c.SVECTOR, &vector));
                c.func_800B6500(i, psx.cptr(c.SVECTOR, &vector));
                const busy = @as(c_uint, @bitCast(actor.unkC | actor.unk10.s32)) | (c.func_800A0BE0(i) & 0x4000);
                state = @as(c_int, @intFromBool(busy != 0)) << 6;
            },
            23 => {
                _ = c.func_8009FC60(i, -2, null, -1);
                if (actor.unkA >= 16) {
                    angle = position.unk0.unk4.pad;
                } else {
                    _ = c.func_800A1108(actor.unkA, &target);
                    angle = c.ratan2(
                        (@as(c_int, psx.getBits(&target.unk0.unk0.fields, "unk0_0")) -% bytesOfC(actor)[0]) << 12,
                        (@as(c_int, psx.getBits(&target.unk0.unk0.fields, "unk0_16")) -% bytesOfC(actor)[2]) << 12,
                    ) +% 0x800;
                }
                c.func_800B6580(i, &actor.unkC, angle);
                state = 0x40;
            },
            16 => {
                _ = c.func_8009FC60(i, -2, null, -1);
                if (psx.get(actor.stats).?.unk32 == 0x10) {
                    speed = @as(c_int, psx.get(actor.stats).?.unk33) << 12;
                } else {
                    speed = @as(c_int, psx.get(actor.stats).?.unk31) << 12;
                }
                _ = c.VectorNormalSS(@ptrCast(&actor.unkC), psx.cptr(c.SVECTOR, &vector));
                if (actor.unkA >= 16) {
                    angle = c.ratan2(vector.vx, vector.vz) +% 0x800;
                } else {
                    _ = c.func_800A1108(actor.unkA, &target);
                    angle = angleToTarget(&target, &position);
                }
                c.func_800B653C(i, psx.cptr(c.SVECTOR, &vector), speed, angle);
                state = 0;
            },
            17 => {
                _ = c.func_8009FC60(i, -2, null, -1);
                if (psx.get(actor.stats).?.unk32 == 0x20) {
                    speed = @as(c_int, psx.get(actor.stats).?.unk33) << 12;
                } else {
                    speed = @as(c_int, psx.get(actor.stats).?.unk31) << 12;
                }
                _ = c.VectorNormalSS(@ptrCast(&actor.unkC), psx.cptr(c.SVECTOR, &vector));
                if (actor.unkA >= 16) {
                    angle = c.ratan2(vector.vx, vector.vz) +% 0x800;
                } else {
                    _ = c.func_800A1108(actor.unkA, &target);
                    angle = angleToTarget(&target, &position);
                }
                c.func_800B653C(i, psx.cptr(c.SVECTOR, &vector), speed, angle);
                state = 0;
            },
            19 => {
                vector.vx = psx.conv(i16, @divTrunc(actor.unkC, 4096) -% position.unk0.unk4.vx);
                vector.vy = psx.conv(i16, @divTrunc(actor.unk14, 4096) -% position.unk0.unk4.vy);
                vector.vz = psx.conv(i16, @divTrunc(actor.unk10.s32, 4096) -% position.unk0.unk4.vz);
                if (actor.unkA >= 16) {
                    angle = c.ratan2(vector.vx, vector.vz) +% 0x800;
                } else {
                    _ = c.func_800A1108(actor.unkA, &target);
                    angle = angleToTarget(&target, &position);
                }
                state = 0;
                c.func_800B6530(i, psx.cptr(c.SVECTOR, &vector), angle);
            },
            6 => {
                c.func_800B64CC(i, position.unk0.unk4.pad, 0, position.unk0.unk4.pad);
                state = 0;
                vector.vy = shortsOfC(actor)[0];
                vector.vx = 0;
                _ = c.func_8009FC60(i, -4, psx.cptr(c.SVECTOR, &vector), shortsOfC(actor)[1]);
            },
            7 => {
                c.func_800B64CC(i, position.unk0.unk4.pad, 0, position.unk0.unk4.pad);
                state = 0;
                vector.vy = shortsOfC(actor)[0];
                vector.vx = 0;
                _ = c.func_8009FC60(i, -4, psx.cptr(c.SVECTOR, &vector), 0x1000);
            },
            22 => {
                if (actor.unkA >= 16) {
                    const unk10_short: *i16 = @ptrCast(&actor.unk10);
                    vector.vx = psx.conv(i16, @divTrunc(@as(c_int, shortsOfC(actor)[0]), 4096) -% position.unk0.unk4.vx);
                    vector.vy = psx.conv(i16, @divTrunc(@as(c_int, shortsOfC(actor)[1]), 4096) -% position.unk0.unk4.vy);
                    vector.vz = psx.conv(i16, @divTrunc(@as(c_int, unk10_short.*), 4096) -% position.unk0.unk4.vz);
                    angle = c.ratan2(vector.vx, vector.vz) +% 0x800;
                } else {
                    _ = c.func_800A1108(actor.unkA, &target);
                    angle = angleToTarget(&target, &position);
                }
                state = 0x40;
                c.func_800B65F4(i, &actor.unkC, angle);
            },
            21 => {
                if (actor.unkA >= 16) {
                    vector.vx = psx.conv(i16, @divTrunc(@as(c_int, bytesOfC(actor)[0]), 4096) -% psx.getBits(&position.unk0.unk0.fields, "unk0_0"));
                    vector.vy = psx.conv(i16, @divTrunc(@as(c_int, bytesOfC(actor)[1]), 4096) -% psx.getBits(&position.unk0.unk0.fields, "unk0_8"));
                    vector.vz = psx.conv(i16, @divTrunc(@as(c_int, bytesOfC(actor)[2]), 4096) -% psx.getBits(&position.unk0.unk0.fields, "unk0_16"));
                    angle = c.ratan2(vector.vx, vector.vz) +% 0x800;
                } else {
                    _ = c.func_800A1108(actor.unkA, &target);
                    angle = angleToTarget(&target, &position);
                }
                state = 0x40;
                c.func_800B65E8(i, &actor.unkC, angle);
            },
            20 => {
                c.func_800A2574(i, position.unk0.unk4.pad);
                state = 0x40;
            },
            8 => {
                if ((c.func_800A0BE0(i) & 0x200000) == 0) {
                    vector.vx = 0;
                    vector.vy = 0;
                    vector.vz = 0;
                    c.func_800B6500(i, psx.cptr(c.SVECTOR, &vector));
                }
                state = 0;
            },
            24 => {
                _ = c.func_8009FC60(i, -2, null, -1);
                // armor[a][b] with the shorts of unkC as unchecked indexes: the rows are 4 entries long
                const armor: [*]v.vs_battle_actor_unk8C0 = @ptrCast(&psx.get(actor.stats).?.armor);
                const shorts: *[2]u16 = @ptrCast(&actor.unkC);
                state = psx.conv(u16, psx.at(armor, @as(c_uint, shorts[0]) *% 4 +% shorts[1]).id);
                if (state == 0) state = 1;
                c.func_80072EC4(actor.id, psx.conv(u16, state));
                state = @as(c_int, @intFromBool(v.g.D_800F19CC().*.?.unk298C == 0xFF)) << 3;
            },
            else => state = 0, // case 1 and the rest
        }

        actor.unk8 = psx.conv(u8, state);
        if (v.g.D_800F196C().* == 3) {
            c.func_800A4E68(i);
        } else {
            const result = c.func_800751B8(i, @ptrCast(&position.unk0));
            if (result != 0) return result;
        }
    }
    return 0;
}
