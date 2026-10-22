//! Steers a stuck movement state out of a blocked spot by probing the four
//! compass directions around it (src/BATTLE/BATTLE.PRG/71618.c).
const psx = @import("common").psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: func_800DB0BC (src/BATTLE/BATTLE.PRG/71618.c)
/// Detours a stuck movement state one step at a time; returns 1 while the detour continues, else 0.
pub fn detourWhenStuck(state: ?*v.stuckMovementState) callconv(.c) c_int {
    // Scratchpad table of direction deltas (D_800F16EC_t, one byte per entry).
    const dirTable: [*]v.D_800F16EC_t = @ptrFromInt(0x1F8003EC);

    if (v.g.D_800F5920().* == null) {
        const s = state.?;
        if ((s.position.raw & 0xFF00FF) == (s.previous.raw & 0xFF00FF)) {
            s.stuck = 1;
            if (s.detourStep == 0) {
                var angle: c_uint = psx.conv(c_uint, c.ratan2(
                    @as(c_int, s.x) -% @as(c_int, s.destination.p.x) *% 128 -% 64,
                    @as(c_int, s.z) -% @as(c_int, s.destination.p.z) *% 128 -% 64,
                ));
                angle &= 0xFFF;
                const first: c_int = psx.conv(c_int, (angle >> 9) & 7);
                const second: c_int = psx.conv(c_int, ((angle >> 9) +% 2) & 7);
                const quadrant: c_uint = angle >> 10;
                const sel: c_uint = if ((angle & 0x3FF) > 512)
                    ((quadrant +% 1) & 3) *% 2
                else
                    quadrant *% 2;

                var directions: [4]u8 = undefined;
                if (sel == psx.conv(c_uint, first)) {
                    directions[0] = psx.conv(u8, first);
                    directions[1] = psx.conv(u8, second);
                } else {
                    directions[0] = psx.conv(u8, second);
                    directions[1] = psx.conv(u8, first);
                }
                if (c.func_800E45D4(2) != 0) {
                    directions[2] = psx.conv(u8, (first +% 4) & 7);
                    directions[3] = psx.conv(u8, (second +% 4) & 7);
                } else {
                    directions[2] = psx.conv(u8, (second +% 4) & 7);
                    directions[3] = psx.conv(u8, (first +% 4) & 7);
                }
                s.detourDirections = directions;

                var i: c_int = 0;
                while (i < 4) : (i +%= 1) {
                    const dir = directions[@intCast(i)];
                    // probe bytes 0 and 2 hold x and z; bytes 1 and 3 stay 0.
                    const xb = psx.conv(u8, @as(c_int, s.position.p.x) +%
                        @as(c_int, psx.getBits(&dirTable[dir], "dx")));
                    const zb = psx.conv(u8, @as(c_int, s.position.p.z) +%
                        @as(c_int, psx.getBits(&dirTable[dir], "dz")));
                    const probe: c_uint = @as(c_uint, xb) | (@as(c_uint, zb) << 16);
                    if (c.func_800D954C(@ptrCast(s), probe) > 0) {
                        s.detourStep = psx.conv(u8, i);
                        break;
                    }
                }
                s.detourStep +%= 1;
            }
            if (s.detourStep >= 5) {
                c.func_800E4CE8(@ptrCast(s));
                s.unk1C = 0;
                s.detourStep = 0;
                return 0;
            }

            // detourStep is 1..4 here (set above or left nonzero), so the index is 0..3.
            const step: usize = @intCast(@as(c_int, s.detourStep) - 1);
            const dir = s.detourDirections[step];
            s.destination.p.x = psx.conv(u8, @as(c_int, s.position.p.x) +%
                @as(c_int, psx.getBits(&dirTable[dir], "dx")));
            s.destination.p.z = psx.conv(u8, @as(c_int, s.position.p.z) +%
                @as(c_int, psx.getBits(&dirTable[dir], "dz")));
            s.unk15C = s.unkFB;
            if (c.func_800D954C(@ptrCast(s), s.destination.raw) > 0) {
                c.func_800E50A0(s, 0, 0);
            } else {
                s.detourStep +%= 1;
            }
            return 1;
        }
        s.detourStep = 0;
    }
    return 0;
}
