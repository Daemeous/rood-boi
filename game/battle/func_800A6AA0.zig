//! Chooses and queues the movement animation of a battle actor from its speed,
//! animation and heading quadrant (src/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A6AA0 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Queues the movement animation for an actor moving at `speed`, from its animation and heading quadrant.
pub fn func_800A6AA0(actor: ?*v.D_800F4538_t, speed: c_int, _: c_int) callconv(.c) c_int {
    const a = actor.?;

    var animation: c_int = a.animationId;
    if (animation >= 100) {
        animation -= 100;
    }
    // The animation id is never negative here, so the unsigned view is the same bits.
    const ua: c_uint = @bitCast(animation);

    var quadrant: c_int = 0;
    if (a.unk1848.unk0 == 1) {
        quadrant = @as(c_int, a.unk1848.unk6) -% @as(c_int, psx.conv(i16, a.unk1848.unk4)) +% 0x200;
        quadrant &= 0xFFF;
        quadrant = @divTrunc(quadrant, 1024);
    }

    const flags: c_uint = psx.byteAt(c_uint, a, 0x5AC).*;

    var next: c_int = 0;
    var frames: c_int = 0;
    var play_pending = false;

    if ((flags & 8) != 0) {
        next = 0x54;
        if (speed > 0) {
            frames = 4;
        } else {
            next = 0x4B;
            frames = 10;
        }
        if (animation == 0x5F) return 0;
        play_pending = true;
    } else if ((psx.byteAt(c_uint, a, 8).* & 0x400000) != 0) {
        if (speed >= @as(c_int, a.unk5B9)) {
            next = @as(c_int, v.g.D_800E90D0()[@intCast(quadrant)]);
            frames = 20;
        } else if (speed > 0) {
            next = @as(c_int, v.g.D_800E90CC()[@intCast(quadrant)]);
            frames = 20;
        } else {
            next = 1;
            frames = 10;
        }
        play_pending = true;
    } else if ((flags & 0x8000) != 0) {
        if (ua -% 0x19 < 2) return 0;
        next = 0x1A;
        if (ua -% 0x12 >= 2) {
            if (animation == 0x14 or animation == 0x11) {
                next = 0x19;
            } else {
                psx.byteAt(c_uint, a, 0x5AC).* = flags & ~@as(c_uint, 0x8000);
                return 0;
            }
        }
        // queue: the entry is written by func_800AD494; null means none was found.
        var entry: [*c]c.u_short = null;
        _ = c.func_800AD494(@ptrCast(a), next, &entry);
        if (entry == null) next = 6;
        _ = c.func_800A0204(a.unk0.unkF, next, 0, 8);
        return 0;
    } else if (speed >= @as(c_int, a.unk5B9) - 1) {
        if (ua -% 0x13 < 2) return 0;
        if ((ua -% 0xF < 4) or (ua -% 0x2E < 11) or (ua -% 9 < 4) or animation == 1 or animation == 6) {
            const old = psx.byteAt(c_uint, a, 0x5AC).*;
            const state: c_uint = (old >> 25) & 1;
            var result: c_uint = old & ~@as(c_uint, 0x02000000);
            result |= (state ^ 1) << 25;
            psx.byteAt(c_uint, a, 0x5AC).* = result;
            _ = c.func_800A0204(a.unk0.unkF, @as(c_int, @intCast(state)) +% 0x13, 0, 6);
            return 0;
        }
        next = 0x13;
        if (animation == 0xD or animation == 0xF) {
            next = 0x14;
        }
        _ = c.func_800A01C8(a.unk0.unkF, next, 4, 0);
        return 0;
    } else if (speed > 0) {
        // false: walk_start; true: walk_toggle.
        var do_toggle = true;
        if ((ua -% 0x2E >= 11) and (ua -% 9 >= 4)) {
            if (ua -% 0xD < 2) {
                if (quadrant != 2) return 0;
            } else if (ua -% 0xF < 2) {
                if (quadrant == 2) return 0;
            } else {
                if (animation == 1 or animation == 6 or animation == 0x11 or animation == 0x12) {
                    // walk_toggle
                } else {
                    do_toggle = false;
                }
            }
        }
        if (do_toggle) {
            // walk_toggle
            const walkState: c_uint = psx.byteAt(c_uint, a, 0x5AC).*;
            const phase: c_uint = (walkState >> 25) & 1;
            const step: u8 = psx.conv(u8, phase);
            next = @as(c_int, step) +% 0xD;
            psx.byteAt(c_uint, a, 0x5AC).* = (walkState & ~@as(c_uint, 0x02000000)) | ((phase ^ 1) << 25);
            if (quadrant == 2) {
                next = @as(c_int, step) +% 0xF;
            }
            _ = c.func_800A0204(a.unk0.unkF, next, 0, 6);
            return 0;
        } else {
            // walk_start
            next = 0xD;
            if (animation == 0x13 or animation == 0xF or animation == 0xD) {
                next += 1;
            }
            if (quadrant == 2) {
                next += 2;
            }
            _ = c.func_800A01C8(a.unk0.unkF, next, 4, 0);
            return 0;
        }
    } else {
        if (ua -% 0xD < 4) {
            _ = c.func_800A0204(a.unk0.unkF, 6, 0, 10);
            _ = c.func_800A01C8(a.unk0.unkF, 1, 10, 0);
            return 0;
        }
        if (animation != 1 and a.unk1848.unk0 != 6) {
            _ = c.func_800A0204(a.unk0.unkF, 1, 0, 10);
            a.unk5B3 = 0;
        }
        a.unk5B3 = psx.conv(u8, @as(c_int, a.unk5B3) +% @divTrunc(v.g.vs_gametime_tickspeed().*, 2));
        if (a.unk5B3 >= 250) {
            a.unk5B3 = 250;
        }
    }

    if (play_pending) {
        _ = c.func_800A0204(a.unk0.unkF, next, 0, frames);
    }
    return 0;
}
