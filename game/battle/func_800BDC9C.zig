//! The event camera's projection, clip and shake opcodes (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");

/// orig: func_800BDC9C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcodes 234..236 set the projection distance, near clip or far clip at once (script[3] 0) or start
/// a ramp to it over script[2]; 239 stops both unk1A4 slots (script[2] 0) or fills the first free one.
pub fn func_800BDC9C(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const cam = v.g.D_800F4BA4().*.?;
    switch (script[0]) {
        234 => {
            const r = &cam.projectionDistance;
            r.unk9 = script[3];
            if (r.unk9 == 0) {
                r.unk2 = if (script[1] != 0) psx.conv(i16, @as(c_int, script[1]) << 2) else 4096 / 8;
                r.unk0 = 1;
            } else {
                if (script[1] != 0) {
                    r.unk4 = r.unk2;
                    r.unk6 = psx.conv(i16, (@as(c_int, script[1]) << 2) - r.unk4);
                } else {
                    r.unk6 = 4096 / 8;
                }
                r.unk1 = script[2];
                r.unk8 = 0;
            }
            return 0;
        },
        235 => {
            const r = &cam.nearClip;
            r.unk9 = script[3];
            if (r.unk9 == 0) {
                r.unk2 = if (script[1] != 0) script[1] else 64;
                r.unk0 = 1;
            } else {
                const near: c_int = if (script[1] != 0) script[1] else 64;
                r.unk4 = r.unk2;
                r.unk6 = psx.conv(i16, near - r.unk4);
                r.unk1 = script[2];
                r.unk8 = 0;
            }
            return 0;
        },
        236 => {
            const r = &cam.farClip;
            r.unk9 = script[3];
            if (r.unk9 == 0) {
                r.unk2 = if (script[1] != 0) psx.conv(i16, @as(c_int, script[1]) << 7) else 4096;
                r.unk0 = 1;
            } else {
                if (script[1] != 0) {
                    r.unk4 = r.unk2;
                    r.unk6 = psx.conv(i16, (@as(c_int, script[1]) << 7) - r.unk4);
                } else {
                    r.unk6 = 4096;
                }
                r.unk1 = script[2];
                r.unk8 = 0;
            }
            return 0;
        },
        239 => {
            // (func_800BDF6C_t*)&unk1A4.unk2: two 12-byte slots from byte 0x1A6
            const entries: [*]v.func_800BDF6C_t = @ptrCast(&cam.unk1A4.unk2);
            if (script[2] == 0) {
                entries[1].unk9 = 0;
                entries[0].unk9 = 0;
                return 0;
            }
            var i: usize = 0;
            while (i < 2) : (i += 1) {
                const entry = &entries[i];
                if (entry.unk9 == 0) {
                    entry.unk4 = script[1];
                    entry.unk6 = script[2];
                    entry.unkA = script[3];
                    entry.unk0.u8[1] = script[4];
                    entry.unk9 = script[5];
                    entry.unk8 = 0;
                    break;
                }
            }
            return 0;
        },
        // No case (opcode 238 is in the table): the C falls off the end. The 64-bit object
        // returns script[0], the 32-bit one the script's address (the interpreter jumps to it).
        else => return if (comptime @sizeOf(usize) == 8) script[0] else @bitCast(psx.addr(script)),
    }
}
