//! The da4bc decision step: steers toward the target or falls back to the candidate search (src/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: func_800DA738 (src/BATTLE/BATTLE.PRG/71618.c)
/// Steers the da4bc state toward its target, falling back to the candidate search when that fails.
pub fn func_800DA738(state: ?*v.da4bcState) callconv(.c) c_int {
    var point: v.da4bcPoint = undefined;
    const candidate: c_int = c.func_800DBB2C(@ptrCast(state), @ptrCast(&point), 3);
    const s = state.?;
    const target: c_int = @as(c_int, psx.getBits(s, "target16"));
    const valid = (psx.conv(c_uint, ~candidate) >> 31) != 0;
    var active: c_int = 0;
    if (@as(c_int, s.id) != target and target != 16) {
        // psx_slot_get(da4bcState*, 0x1F80037C, target): a guest pointer slot in the scratchpad.
        const slot0: u32 = @as(*align(1) const u32, @ptrFromInt(@as(usize, psx.conv(u32, @as(c_int, 0x1F80037C) +% (4 *% target))))).*;
        if (slot0 != 0) {
            active = @as(c_int, @intFromBool((psx.conv(u8, psx.at(&s.targets, target).*) >> 6) == 2));
        }
    }
    const oldFlag: c_int = s.flagF;
    s.flagF = psx.conv(u8, active);
    blk: {
        if (valid) {
            if (active != 0) {
                if (((psx.at(&s.targets, target).* >> 5) & 1) != 0) {
                    const first: *align(1) v.da4bcModel = @ptrCast(psx.at(v.g.D_800F4538(), target).*.?);
                    const tile = first.tile;
                    const angleA: c_int = c.ratan2(
                        (@as(c_int, point.b[0]) << 7) -% (@as(c_int, first.x) -% 64),
                        (@as(c_int, point.b[2]) << 7) -% (@as(c_int, first.z) -% 64),
                    );
                    const second: *align(1) v.da4bcModel = @ptrCast(psx.at(v.g.D_800F4538(), target).*.?);
                    const angleB: c_int = c.ratan2(
                        @as(c_int, s.x) -% @as(c_int, second.x),
                        @as(c_int, s.z) -% @as(c_int, second.z),
                    );
                    const cosA = c.rcos(angleA);
                    const cosB = c.rcos(angleB);
                    const sinA = c.rsin(angleA);
                    var dot: c_int = cosA *% cosB +% sinA *% c.rsin(angleB);
                    dot >>= 12;
                    if (s.timer44 != 0) s.timer44 -%= 1;
                    if (dot >= -1200 or s.timer44 != 0) {
                        // The tile word is passed by value: its bits are the C union's bits.
                        const tileBits: c.vs_battle_movementPosition = @bitCast(tile.raw);
                        c.func_800E1388(@ptrCast(s), tileBits, c.ratan2(
                            @as(c_int, point.b[0]) -% psx.conv(c_int, tile.raw & 255),
                            @as(c_int, point.b[2]) -% psx.conv(c_int, (tile.raw >> 16) & 255),
                        ), 320);
                        psx.byteAt(u8, s, 1).* = 1;
                        c.func_800E50A0(@ptrCast(s), 0, 0);
                        if (s.timer44 == 0) s.timer44 = 10;
                        return 1;
                    }
                }
            } else break :blk;
        }
        if (active != 0) {
            if (valid) {
                const slotB_raw: u32 = @as(*align(1) const u32, @ptrFromInt(@as(usize, psx.conv(u32, @as(c_int, 0x1F80037C) +% (4 *% target))))).*;
                const slotB: *align(1) v.da4bcState = @ptrFromInt(@as(usize, slotB_raw));
                if (psx.at(&slotB.distances, candidate).* <= 0x23FFF) {
                    s.target15C = psx.conv(u8, target);
                    c.func_800E2CCC(@ptrCast(s));
                    return 1;
                }
            }
            if (c.func_800DB370(@ptrCast(s), target, 320, 1) != 0) return 1;
            if (c.func_800DB74C(@ptrCast(s), target) != 0) return 1;
        }
    }
    // fallback:
    if (valid) {
        if (oldFlag != 0) {
            if (psx.conv(c_uint, c.func_800E45D4(128)) < 128) s.flag18 = 1;
            c.func_800D8260(@ptrCast(s), 6, 0);
        }
        if (c.func_800DB61C(@ptrCast(s), 1) != 0) return 1;
    }
    return c.func_800DB61C(@ptrCast(s), 1);
}
