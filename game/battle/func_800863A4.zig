//! An action request aimed at a target, resolved by func_80085B10 (src/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_800863A4 (src/BATTLE/BATTLE.PRG/146C.c)
/// Builds a request for action `action` by actor `user` on `target`: for area kinds (2, 4, 6, 8: the action's
/// area at `to`; 11, 12: its range aimed from `from` to `to`) every limb of the target inside the area, for kinds
/// 5 and 7 limb `limb`, for 1..4 the target's own limb (unk36); resolves it into `result` (func_80085B10). Returns
/// the top byte of the action's cost for the user.
pub fn func_800863A4(action: c_int, user: c_int, target: c_int, limb: c_int, from: ?*v.SVECTOR, to: ?*v.SVECTOR, result: ?*v.D_800F19CC_t2) callconv(.c) c_int {
    // ROOD_PC: the request is cleared first (its unwritten parts were stack residue, pc/docs/Oracle.md).
    var request = std.mem.zeroes(v.D_800F19CC_t2);
    // The C declares `u_short sp870[2]` directly below `func_800C1564_t sp860` (the host compiler's layout, 0x2C and
    // 0x30 from the stack pointer), but func_800A1B9C writes three halfwords: the third overwrites the area's first two
    // bytes (shape and unk1) before func_800C1564 reads both. The PS1 frame has 8 bytes there, so it does not; this keeps
    // the host C's behaviour (the enemies' casts the corpus recipes witness depend on it).
    var frame: extern struct { point: [2]u16, area: v.func_800C1564_t } = undefined;
    const area = &frame.area;
    const point = &frame.point;
    request.actionIndex = psx.conv(u16, action);
    request.unk4.unk40 = 0;
    request.unk4.unk0.targetActor = psx.conv(u8, user);
    request.unk4A = 0;
    // vs_main_actions has 256 entries in the views; the action index is not bounded by the C: psx.at.
    const spec = psx.at(v.g.actions(), action);
    switch (psx.getBits(spec, "unk2_4")) {
        11, 12 => {
            area.unk0 = psx.getBits(spec, "shape");
            area.unk1 = 1;
            area.unk4.values[0] = spec.rangeX;
            area.unk4.values[1] = spec.rangeY;
            area.unk4.values[2] = spec.rangeZ;
            const dx = @as(c_int, to.?.vx) - from.?.vx;
            const dz = @as(c_int, to.?.vz) - from.?.vz;
            area.unk2 = psx.conv(i16, c.ratan2(dx, dz));
            const dy = @as(c_int, to.?.vy) - from.?.vy;
            const distance = c.vs_gte_rsqrt(dx *% dx +% dz *% dz);
            area.unk4.values[3] = psx.conv(u8, @divTrunc(c.ratan2(dy, distance), 16) +% 0x40);
            area.unk8 = from.?.*;
        },
        2, 4, 6, 8 => {
            area.unk0 = spec.aoe_24 & 7;
            area.unk1 = 1;
            area.unk4.values[0] = spec.aoe_0;
            area.unk4.values[1] = spec.aoe_8;
            area.unk4.values[2] = spec.aoe_16;
            area.unk2 = 0;
            area.unk4.values[3] = spec.aoe_24 & 0xF8;
            area.unk8 = to.?.*;
        },
        else => {},
    }
    switch (psx.getBits(spec, "unk2_4")) {
        6, 8, 11, 12 => {
            var count: c_int = 0;
            var i: c_int = 0;
            while (i < 6) : (i += 1) { // i < 6
                if (c.func_800A1B9C(target, i, point, 2) >= 0 and c.func_800C1564(@ptrCast(area), point) != 0) {
                    // unk4C has 30 entries: at most 6 here
                    request.unk4C[@intCast(count)].unk40 = 0;
                    request.unk4C[@intCast(count)].unk0.targetActor = psx.conv(u8, target);
                    request.unk4C[@intCast(count)].unk0.targetLimb = psx.conv(i8, i);
                    count += 1;
                }
            }
            request.unk4A = psx.conv(u16, count);
        },
        5, 7 => {
            request.unk4C[0].unk40 = 0;
            request.unk4C[0].unk0.targetActor = psx.conv(u8, target);
            request.unk4C[0].unk0.targetLimb = psx.conv(i8, limb);
            request.unk4A = 1;
        },
        1, 2, 3, 4 => {
            request.unk4C[0].unk40 = 0;
            request.unk4C[0].unk0.targetActor = psx.conv(u8, target);
            request.unk4C[0].unk0.targetLimb = psx.conv(i8, psx.getBits(psx.get(psx.at(v.g.vs_battle_actors(), target).*.?.stats).?, "unk36"));
            request.unk4A = 1;
        },
        else => {},
    }
    c.func_80085B10(action, @ptrCast(result), @ptrCast(&request), 0);
    return c._getActionCost(psx.conv(c_uint, action), @ptrCast(psx.get(psx.at(v.g.vs_battle_actors(), user).*.?.stats)), 0) & @as(c_int, @bitCast(@as(c_uint, 0xFF000000)));
}
