//! Script opcodes 185-237 and 191: camera, fade, colour and screen-effect commands
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BD6C4 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs the camera, fade, colour or screen-effect opcode at script; returns 1 while a timed effect in progress blocks it.
pub fn func_800BD6C4(script: [*c]u8, _: c_short) callconv(.c) c_int {
    // uninitialised in the original: the callees copy the whole struct, but only the fields written here are read.
    var values: v.D_800F1A68_t = .{ .unk0 = 0, .unk4 = 0, .unk8 = 0, .unkC = 0 };
    var color: v.P_CODE = .{ .r0 = 0, .g0 = 0, .b0 = 0, .code = 0 };

    switch (script[0]) {
        185 => {
            c.vs_render_setFramebufferEffectDepth(psx.conv(c_uint, c.vs_battle_getShort(script + 1)));
            return 0;
        },
        186 => {
            c.vs_render_setFramebufferEffectPreviousFrame(@as(c_int, script[1]));
            return 0;
        },
        228 => {
            v.g.vs_event_camera().*.?.unk1FC.unk1E = @as(i16, script[4]);
            if (v.g.vs_event_camera().*.?.unk1FC.unk1E == 0) {
                values.unk0 = @as(c_int, script[1]) << 7;
                values.unk4 = @as(c_int, script[2]) << 7;
                c.vs_render_setFramebufferEffectScale(psx.cptr(c.D_800F1A68_t, &values));
            } else {
                v.g.vs_event_camera().*.?.unk1FC.unk1C = 0;
                c.vs_render_readFramebufferEffectScale(&values.unk0);
                const st = v.g.vs_event_camera().*.?;
                st.unk1FC.unkA = psx.conv(i16, values.unk0);
                st.unk1FC.unk12 = psx.conv(i16, (@as(c_int, script[1]) << 7) -% @as(c_int, st.unk1FC.unkA));
                st.unk1FC.unkC = psx.conv(i16, values.unk4);
                st.unk1FC.unk14 = psx.conv(i16, (@as(c_int, script[2]) << 7) -% @as(c_int, st.unk1FC.unkC));
                st.unk1FC.unk1 = script[3];
            }
            return 0;
        },
        229 => {
            v.g.vs_event_camera().*.?.unk21C.unk1E = @as(i16, script[5]);
            if (v.g.vs_event_camera().*.?.unk21C.unk1E == 0) {
                color.r0 = script[1];
                color.g0 = script[2];
                color.b0 = script[3];
                c.vs_render_setFramebufferEffectTint(psx.cptr(c.P_CODE, &color));
            } else {
                v.g.vs_event_camera().*.?.unk21C.unk1C = 0;
                c.vs_render_readFramebufferEffectTint(psx.cptr(c.P_CODE, &color));
                const st = v.g.vs_event_camera().*.?;
                st.unk21C.unkA = @as(i16, color.r0);
                st.unk21C.unk12 = psx.conv(i16, @as(c_int, script[1]) -% @as(c_int, st.unk21C.unkA));
                st.unk21C.unkC = @as(i16, color.g0);
                st.unk21C.unk14 = psx.conv(i16, @as(c_int, script[2]) -% @as(c_int, st.unk21C.unkC));
                st.unk21C.unkE = @as(i16, color.b0);
                st.unk21C.unk16 = psx.conv(i16, @as(c_int, script[3]) -% @as(c_int, st.unk21C.unkE));
                st.unk21C.unk1 = script[4];
            }
            return 0;
        },
        230 => {
            v.g.vs_event_camera().*.?.unk23C.unk1E = @as(i16, script[4]);
            if (v.g.vs_event_camera().*.?.unk23C.unk1E == 0) {
                values.unk0 = psx.conv(c_int, psx.conv(i8, script[1]));
                values.unk4 = psx.conv(c_int, psx.conv(i8, script[2]));
                c.vs_render_setFramebufferEffectOffset(psx.cptr(c.D_800F1A68_t, &values));
            } else {
                v.g.vs_event_camera().*.?.unk23C.unk1C = 0;
                c.vs_render_readFramebufferEffectOffset(psx.cptr(c.D_800F1A68_t, &values));
                const st = v.g.vs_event_camera().*.?;
                st.unk23C.unkA = psx.conv(i16, values.unk0);
                st.unk23C.unk12 = psx.conv(i16, @as(c_int, psx.conv(i8, script[1])) -% @as(c_int, st.unk23C.unkA));
                st.unk23C.unkC = psx.conv(i16, values.unk4);
                st.unk23C.unk14 = psx.conv(i16, @as(c_int, psx.conv(i8, script[2])) -% @as(c_int, st.unk23C.unkC));
                st.unk23C.unk1 = script[3];
            }
            return 0;
        },
        231 => {
            c.vs_render_setFramebufferEffectBlendMode(@as(c_int, script[1]));
            return 0;
        },
        237 => {
            if (v.g.vs_event_camera().*.?.unk25C.unk1E > 0) {
                return 1;
            }
            v.g.vs_event_camera().*.?.unk25C.unk1E = @as(i16, script[4]);
            if (v.g.vs_event_camera().*.?.unk25C.unk1E == 0) {
                const effectValue: i16 = psx.conv(i16, @as(c_int, script[1]) << 6);
                const st = v.g.vs_event_camera().*.?;
                st.unk25C.unk2 = effectValue;
                st.unk25C.unk4 = @as(i16, psx.conv(i8, script[2]));
                // ROOD_PC: the screen-effect warp hook (func_800F9BC0 on the PS1).
                c.pc_screff2_set_warp(@as(c_int, effectValue), @as(c_int, st.unk25C.unk4));
            } else {
                const st = v.g.vs_event_camera().*.?;
                st.unk25C.unk1C = 0;
                st.unk25C.unkA = st.unk25C.unk2;
                st.unk25C.unk12 = psx.conv(i16, (@as(c_int, script[1]) << 6) -% @as(c_int, st.unk25C.unkA));
                st.unk25C.unkC = st.unk25C.unk4;
                st.unk25C.unk14 = psx.conv(i16, @as(c_int, psx.conv(i8, script[2])) -% @as(c_int, st.unk25C.unkC));
                st.unk25C.unk1 = script[3];
            }
            return 0;
        },
        191 => {
            const mode: c_short = c.vs_battle_getShort(script + 1);
            if (mode == 0) {
                c.vs_render_setFramebufferEffectBlendMode(0);
                values.unk0 = 0;
                values.unk4 = 0;
                c.vs_render_setFramebufferEffectOffset(psx.cptr(c.D_800F1A68_t, &values));
                values.unk0 = 4096;
                values.unk4 = 4096;
                c.vs_render_setFramebufferEffectScale(psx.cptr(c.D_800F1A68_t, &values));
                color.r0 = 128;
                color.g0 = 128;
                color.b0 = 128;
                c.vs_render_setFramebufferEffectTint(psx.cptr(c.P_CODE, &color));
                c.vs_render_setFramebufferEffectDepth(0);
                c.vs_render_setFramebufferEffectPreviousFrame(0);
                c.vs_render_setFramebufferEffectEnabled(0);
            } else {
                c.vs_render_setFramebufferEffectEnabled(1);
                c.vs_render_setFramebufferEffectBlendMode(0);
                values.unk0 = 0;
                values.unk4 = 2;
                c.vs_render_setFramebufferEffectOffset(psx.cptr(c.D_800F1A68_t, &values));
                values.unk0 = 4096;
                values.unk4 = 4096;
                c.vs_render_setFramebufferEffectScale(psx.cptr(c.D_800F1A68_t, &values));
                color.r0 = 144;
                color.g0 = 144;
                color.b0 = 144;
                c.vs_render_setFramebufferEffectPreviousFrame(1);
                c.vs_render_setFramebufferEffectTint(psx.cptr(c.P_CODE, &color));
                c.vs_render_setFramebufferEffectDepth(psx.conv(c_uint, mode));
            }
            return 0;
        },
        // No case matches: the original falls off the end and returns the object's %eax. Outside the jump
        // table's range 185..237 that is the opcode + 0x47 (both targets); inside it, the address of the
        // default code, which is not reproducible: 0 here.
        else => return if (script[0] >= 185 and script[0] <= 237)
            0
        else
            @as(c_int, script[0]) +% 0x47,
    }
}
