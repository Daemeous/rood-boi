//! Script opcodes 185-237 and 191: camera, fade, colour and screen-effect commands
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BD6C4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs the camera, fade, colour or screen-effect opcode at script; returns 1 while a timed effect in progress blocks it.
pub fn func_800BD6C4(script: [*c]u8, _: c_short) callconv(.c) c_int {
    // Zero-initialised: the original's locals are only partly written before a callee fills them.
    var values: v.D_800F1A68_t = .{ .unk0 = 0, .unk4 = 0, .unk8 = 0, .unkC = 0 };
    var color: v.P_CODE = .{ .r0 = 0, .g0 = 0, .b0 = 0, .code = 0 };

    switch (script[0]) {
        185 => {
            c.func_8007DE44(psx.conv(c_uint, c.vs_battle_getShort(script + 1)));
            return 0;
        },
        186 => {
            c.func_8007DE5C(@as(c_int, script[1]));
            return 0;
        },
        228 => {
            v.g.D_800F4BA4().*.?.unk1FC.unk1E = @as(i16, script[4]);
            if (v.g.D_800F4BA4().*.?.unk1FC.unk1E == 0) {
                values.unk0 = @as(c_int, script[1]) << 7;
                values.unk4 = @as(c_int, script[2]) << 7;
                c.func_8007DDB8(psx.cptr(c.D_800F1A68_t, &values));
            } else {
                v.g.D_800F4BA4().*.?.unk1FC.unk1C = 0;
                c.func_8007DE88(&values.unk0);
                const st = v.g.D_800F4BA4().*.?;
                st.unk1FC.unkA = psx.conv(i16, values.unk0);
                st.unk1FC.unk12 = psx.conv(i16, (@as(c_int, script[1]) << 7) -% @as(c_int, st.unk1FC.unkA));
                st.unk1FC.unkC = psx.conv(i16, values.unk4);
                st.unk1FC.unk14 = psx.conv(i16, (@as(c_int, script[2]) << 7) -% @as(c_int, st.unk1FC.unkC));
                st.unk1FC.unk1 = script[3];
            }
            return 0;
        },
        229 => {
            v.g.D_800F4BA4().*.?.unk21C.unk1E = @as(i16, script[5]);
            if (v.g.D_800F4BA4().*.?.unk21C.unk1E == 0) {
                color.r0 = script[1];
                color.g0 = script[2];
                color.b0 = script[3];
                c.func_8007DDD4(psx.cptr(c.P_CODE, &color));
            } else {
                v.g.D_800F4BA4().*.?.unk21C.unk1C = 0;
                c.func_8007DEA8(psx.cptr(c.P_CODE, &color));
                const st = v.g.D_800F4BA4().*.?;
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
            v.g.D_800F4BA4().*.?.unk23C.unk1E = @as(i16, script[4]);
            if (v.g.D_800F4BA4().*.?.unk23C.unk1E == 0) {
                values.unk0 = psx.conv(c_int, psx.conv(i8, script[1]));
                values.unk4 = psx.conv(c_int, psx.conv(i8, script[2]));
                c.func_8007DDF8(psx.cptr(c.D_800F1A68_t, &values));
            } else {
                v.g.D_800F4BA4().*.?.unk23C.unk1C = 0;
                c.func_8007DECC(psx.cptr(c.D_800F1A68_t, &values));
                const st = v.g.D_800F4BA4().*.?;
                st.unk23C.unkA = psx.conv(i16, values.unk0);
                st.unk23C.unk12 = psx.conv(i16, @as(c_int, psx.conv(i8, script[1])) -% @as(c_int, st.unk23C.unkA));
                st.unk23C.unkC = psx.conv(i16, values.unk4);
                st.unk23C.unk14 = psx.conv(i16, @as(c_int, psx.conv(i8, script[2])) -% @as(c_int, st.unk23C.unkC));
                st.unk23C.unk1 = script[3];
            }
            return 0;
        },
        231 => {
            c.func_8007DE2C(@as(c_int, script[1]));
            return 0;
        },
        237 => {
            if (v.g.D_800F4BA4().*.?.unk25C.unk1E > 0) {
                return 1;
            }
            v.g.D_800F4BA4().*.?.unk25C.unk1E = @as(i16, script[4]);
            if (v.g.D_800F4BA4().*.?.unk25C.unk1E == 0) {
                const effectValue: i16 = psx.conv(i16, @as(c_int, script[1]) << 6);
                const st = v.g.D_800F4BA4().*.?;
                st.unk25C.unk2 = effectValue;
                st.unk25C.unk4 = @as(i16, psx.conv(i8, script[2]));
                // ROOD_PC: the screen-effect warp hook (func_800F9BC0 on the PS1).
                c.pc_screff2_set_warp(@as(c_int, effectValue), @as(c_int, st.unk25C.unk4));
            } else {
                const st = v.g.D_800F4BA4().*.?;
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
                c.func_8007DE2C(0);
                values.unk0 = 0;
                values.unk4 = 0;
                c.func_8007DDF8(psx.cptr(c.D_800F1A68_t, &values));
                values.unk0 = 4096;
                values.unk4 = 4096;
                c.func_8007DDB8(psx.cptr(c.D_800F1A68_t, &values));
                color.r0 = 128;
                color.g0 = 128;
                color.b0 = 128;
                c.func_8007DDD4(psx.cptr(c.P_CODE, &color));
                c.func_8007DE44(0);
                c.func_8007DE5C(0);
                c.func_8007DD50(0);
            } else {
                c.func_8007DD50(1);
                c.func_8007DE2C(0);
                values.unk0 = 0;
                values.unk4 = 2;
                c.func_8007DDF8(psx.cptr(c.D_800F1A68_t, &values));
                values.unk0 = 4096;
                values.unk4 = 4096;
                c.func_8007DDB8(psx.cptr(c.D_800F1A68_t, &values));
                color.r0 = 144;
                color.g0 = 144;
                color.b0 = 144;
                c.func_8007DE5C(1);
                c.func_8007DDD4(psx.cptr(c.P_CODE, &color));
                c.func_8007DE44(psx.conv(c_uint, mode));
            }
            return 0;
        },
        // No case matches: the original falls off the end, so the value it returns is unspecified; 0 here.
        else => return 0,
    }
}
