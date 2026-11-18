//! The event camera's projection, clip and shake opcodes (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");

/// orig: func_800BDC9C (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opcodes 234..236 set the projection distance, near clip or far clip at once (script[3] 0) or start
/// a ramp to it over script[2]; 239 stops both unk1A4 slots (script[2] 0) or fills the first free one.
pub fn func_800BDC9C(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const cam = v.g.vs_event_camera().*.?;
    switch (script[0]) {
        234 => {
            const r = &cam.projectionDistance;
            r.durationFrames = script[3];
            if (r.durationFrames == 0) {
                r.value = if (script[1] != 0) psx.conv(i16, @as(c_int, script[1]) << 2) else 4096 / 8;
                r.hasChanged = 1;
            } else {
                if (script[1] != 0) {
                    r.startValue = r.value;
                    r.deltaValue = psx.conv(i16, (@as(c_int, script[1]) << 2) - r.startValue);
                } else {
                    r.deltaValue = 4096 / 8;
                }
                r.easingMode = script[2];
                r.elapsedFrames = 0;
            }
            return 0;
        },
        235 => {
            const r = &cam.nearClip;
            r.durationFrames = script[3];
            if (r.durationFrames == 0) {
                r.value = if (script[1] != 0) script[1] else 64;
                r.hasChanged = 1;
            } else {
                const near: c_int = if (script[1] != 0) script[1] else 64;
                r.startValue = r.value;
                r.deltaValue = psx.conv(i16, near - r.startValue);
                r.easingMode = script[2];
                r.elapsedFrames = 0;
            }
            return 0;
        },
        236 => {
            const r = &cam.farClip;
            r.durationFrames = script[3];
            if (r.durationFrames == 0) {
                r.value = if (script[1] != 0) psx.conv(i16, @as(c_int, script[1]) << 7) else 4096;
                r.hasChanged = 1;
            } else {
                if (script[1] != 0) {
                    r.startValue = r.value;
                    r.deltaValue = psx.conv(i16, (@as(c_int, script[1]) << 7) - r.startValue);
                } else {
                    r.deltaValue = 4096;
                }
                r.easingMode = script[2];
                r.elapsedFrames = 0;
            }
            return 0;
        },
        239 => {
            // (vs_event_ShakeOscillator*)&unk1A4.unk2: two 12-byte slots from byte 0x1A6
            const entries: [*]v.vs_event_ShakeOscillator = @ptrCast(&cam.unk1A4.displacement);
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
                    entry.axisMask = script[3];
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
        else => return script[0],
    }
}
