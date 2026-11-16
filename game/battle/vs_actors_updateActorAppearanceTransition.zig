//! Battle object colour fade and CLUT animation, with its weapon models'
//! (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const psx = @import("common").psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_updateActorAppearanceTransition (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Runs one frame of an object's colour fade and CLUT animation, and of its weapon models'.
pub fn vs_actors_updateActorAppearanceTransition(arg0: ?*v.D_800F45E0_t) callconv(.c) void {
    const State = enum { block19, block20, block32, block38, block60, tail };
    const a = arg0.?;
    var sp18: v.SVECTOR = undefined;
    var sp20: v.SVECTOR = undefined;
    var sp28: [*c]u16 = null;
    var sp2C: c_int = 0;
    var sp30: [*c]u16 = null;
    var reference: [*c]u16 = null;
    var var_s0: c_int = 0;
    var var_s2: c_int = 0;
    var var_s3: c_int = 0;
    var var_s4: c_int = 0;
    var var_s5: ?*v.D_800F45E0_t = null;
    var var_s6: c_int = 0;
    var var_v0: c_int = 0;
    var var_v1: c_int = 0;
    var var_a1: c_int = 0;
    var var_fp: [*c]u16 = null;
    var weapon0: ?*v.vs_battle_wepModels_t = null;

    // Phase 1: slide the object by the sine of its angle.
    const temp_v1: c_uint = psx.fieldAt(c_uint, &a._bits_unk8_0, 0).*;
    if (((temp_v1 >> 8) & 0xF) < 6) {
        if ((temp_v1 & 0x70000) != 0) {
            var_v0 = c.rsin(0x200) *% @as(c_int, psx.fieldAt(u16, &a.unk5EC, 80).*);
            var_a1 = var_v0;
            if (var_v0 < 0) var_a1 = var_v0 +% 0xFFF;
            c.vs_field_sampleActorCollisionPerimeter(a, var_a1 >> 12);
            c.vs_field_applyActorWallRepulsion(a, &a.unk34);
            const temp_a2: u16 = @bitCast(a.unk38);
            a.unk34 = psx.conv(i16, @divTrunc(@as(c_int, a.unk34), 2));
            a.unk38 = psx.conv(i16, @divTrunc(@as(c_int, psx.conv(i16, temp_a2)), 2));
            c.vs_actors_updateActorJumpAndFall(@as(c_int, a.unkF), @ptrCast(a));
            if ((psx.fieldAt(u16, &a._bits_unk8_0, 2).* & 7) >= 4) {
                if (a.unkF != 0) {
                    _ = c.vs_actors_findActorAnimationHeader(a, 0xAB, &sp28);
                    if (sp28 != null) {
                        psx.fieldAt(u32, &a._bits_unk5AC_0, 0).* &= 0xFEFF_FFFF;
                        c.vs_actors_playActorAnimation(@as(c_int, a.unkF), 0x47, 0, 8);
                        psx.fieldAt(u32, &a._bits_unk5AC_0, 0).* |= 0x0100_0000;
                    }
                }
            }
        }
    }

    // Phase 2: the fade step, from the tick speed.
    const temp_v1_2: c_uint = psx.fieldAt(c_uint, &a._bits_unk8_0, 0).*;
    const tick: c_uint = @bitCast(v.g.vs_gametime_tickspeed().*);
    const tickSum: c_int = @bitCast(tick +% (tick >> 31));
    var_s4 = tickSum >> 1;
    var_s6 = 0;
    if ((temp_v1_2 & 0x80) != 0) {
        var_s6 = 1;
        if ((psx.fieldAt(u32, &a._bits_unk5AC_0, 0).* & 0x0800_0000) != 0) {
            sp2C = 0xA0;
        } else {
            var_s6 = 0;
            sp2C = 0x10;
        }
        const temp_v0: c_int = var_s6 *% 2;
        const fp: *align(2) u16 = @alignCast(psx.fieldAt(u16, &a.unk11A4, @as(isize, temp_v0 +% 0x3C0)));
        var_fp = fp;
        const p30: *align(2) u16 = @alignCast(psx.fieldAt(u16, &a.unk11A4, @as(isize, temp_v0 +% 0x280)));
        sp30 = p30;
    } else {
        var_s5 = a;
        sp2C = 0x10;
        const fp: *align(2) u16 = @alignCast(psx.fieldAt(u16, &a.unk6C, 224));
        var_fp = fp;
        const p30: *align(2) u16 = @alignCast(psx.fieldAt(u16, &a.unk6C, 192));
        sp30 = p30;
        if ((temp_v1_2 & 0xF00) == 0x200) var_s4 *%= 2;
    }

    const mode: c_int = psx.conv(c_int, (psx.fieldAt(c_uint, &a._bits_unk8_0, 0).* >> 8) & 0xF);
    var st: State = undefined;
    switch (mode) {
        2 => {
            var_s2 = 0;
            st = .block19;
        },
        3 => {
            var_s3 = 0;
            var_s2 = 1;
            st = .block20;
        },
        4 => {
            if ((psx.fieldAt(u32, &a._bits_unk5AC_0, 0).* & 8) != 0) {
                var_s5 = psx.at(v.g.vs_field_gimmickInstances(), a.unk13).*;
                const v5 = var_s5.?;
                c.vs_actors_anchorPoint(@as(c_int, v5.unk12), 0xF0, @ptrCast(&sp18), 1);
                c.vs_actors_anchorPoint(@as(c_int, v5.unk12), 0xF1, @ptrCast(&sp20), 1);
                v5.unk1C = a.unk1C;
                v5.unk20 = a.unk20;
                v5.unk1E = psx.conv(i16, @divTrunc(@as(c_int, sp18.vy) + @as(c_int, sp20.vy), 2) +% 0x3C);
                const angle: c_int = @as(c_int, psx.conv(u16, a.unk26));
                v5.unk1A = 0xFC;
                psx.byteAt(i32, v5, 0x34).* = 0;
                v5.unk12 = 0xFF;
                v5.unk26 = psx.conv(i16, (@as(c_int, psx.conv(u16, v5.unk26)) +% angle) & 0xFFF);
                psx.fieldAt(u32, &a._bits_unk5AC_0, 0).* &= 0xFFFF_FFF7;
                v.g.D_800F4B19().* = a.unk13;
            }
            var_v1 = @as(c_int, psx.conv(u8, a.unk54)) -% (var_s4 *% 4);
            var_s3 = 0;
            if (var_v1 < 0) var_v1 = 0;
            if (var_v1 != 0) var_s3 = 1;
            psx.fieldAt(u8, &a.unk54, 0).* = psx.conv(u8, var_v1);
            psx.fieldAt(u8, &a.unk54, 1).* = psx.conv(u8, var_v1);
            var_s0 = @as(c_int, psx.conv(i8, a.unk6F2));
            var_s0 +%= var_s4 *% 2;
            psx.fieldAt(u8, &a.unk54, 2).* = psx.conv(u8, var_v1);
            if (var_s0 >= 0x41) var_s0 = 0x40;
            if (var_s0 != 0x40) var_s3 = 1;
            a.unk6F2 = psx.conv(u8, var_s0);
            st = .block32;
        },
        6 => {
            var_s2 = 3;
            var_v1 = @as(c_int, psx.fieldAt(u8, &a.unk54, 3).*);
            var_v1 +%= 1;
            var_v0 = var_v1 & 255;
            var_s3 = @as(c_int, @intFromBool(psx.conv(c_uint, var_v0) < 15));
            var_s0 = @as(c_int, psx.conv(i8, a.unk6F2));
            var_s0 +%= var_s4 *% 2;
            var_v0 = @as(c_int, @intFromBool(var_s0 < 0x41));
            psx.fieldAt(u8, &a.unk54, 3).* = psx.conv(u8, var_v1);
            st = .block60;
        },
        7 => {
            var_s3 = 0;
            var_s2 = 1;
            var_s0 = @as(c_int, psx.conv(i8, a.unk6F2));
            var_s0 +%= var_s4 *% 2;
            var_v0 = @as(c_int, @intFromBool(var_s0 < 0x41));
            st = .block60;
        },
        9 => {
            var_s3 = 0;
            var_s2 = 2;
            psx.fieldAt(u8, &a.unk54, 0).* = 0xFF;
            psx.fieldAt(u8, &a.unk54, 1).* = 255;
            psx.fieldAt(u8, &a.unk54, 2).* = 255;
            st = .block20;
        },
        10 => {
            var_s3 = 0;
            const temp_v1_5: c_int = @as(c_int, psx.conv(i8, a.unk6F1));
            var_s0 = @as(c_int, psx.conv(i8, a.unk6F2));
            var_s0 -%= var_s4 *% 2;
            var_s2 = 4;
            if (var_s0 < temp_v1_5) var_s0 = temp_v1_5;
            if (var_s0 != temp_v1_5) var_s3 = 1;
            a.unk6F2 = psx.conv(u8, var_s0);
            var_s3 |= c.vs_actors_stepRgbTowardTarget(psx.fieldAt(u8, &a.unk54, 0), psx.fieldAt(u8, &a.unk58.unk0, 0), (0 -% var_s4) *% 8);
            st = .block20;
        },
        11 => {
            psx.fieldAt(u32, &a._bits_unk8_0, 0).* &= 0xFFFF_F0FF;
            return;
        },
        12 => {
            var_s3 = 0;
            var_s2 = 5;
            st = .block20;
        },
        14 => {
            a.unk6F2 = 0x40;
            var_s2 = 6;
            st = .block19;
        },
        else => return,
    }

    while (true) {
        switch (st) {
            .block19 => {
                var_s3 = 0;
                st = .block20;
            },
            .block20 => {
                var_s3 |= c.vs_actors_applyPaletteTransition(var_fp, sp2C -% var_s6, var_s4, var_s2, sp30);
                if ((psx.fieldAt(u32, &a._bits_unk8_0, 0).* & 0x80) != 0) {
                    var_s0 = 0;
                    while (true) {
                        weapon0 = psx.at(v.g.vs_battle_wepModels(), @as(c_int, a.unkF) *% 2 +% var_s0).*;
                        if (weapon0) |w| {
                            reference = &w.unk440;
                            var_s3 |= c.vs_actors_applyPaletteTransition(&w.unk500, @as(c_int, w.nClutColors), var_s4, var_s2, reference);
                        }
                        var_s0 +%= 1;
                        if (!(var_s0 < 2)) break;
                    }
                }
                if (((psx.fieldAt(u32, &a._bits_unk8_0, 0).* & 0xF00) == 0x300) and var_s3 == 0) {
                    a.unk54 = 0x404040;
                    _ = c.vs_actors_applyPaletteTransition(var_fp, sp2C -% var_s6, var_s4, 2, sp30);
                    var_s0 = 0;
                    if ((psx.fieldAt(u32, &a._bits_unk8_0, 0).* & 0x80) != 0) {
                        while (true) {
                            weapon0 = psx.at(v.g.vs_battle_wepModels(), @as(c_int, a.unkF) *% 2 +% var_s0).*;
                            if (weapon0) |w| {
                                reference = &w.unk440;
                                _ = c.vs_actors_applyPaletteTransition(&w.unk500, @as(c_int, w.nClutColors), var_s4, 2, reference);
                            }
                            var_s0 +%= 1;
                            if (!(var_s0 < 2)) break;
                        }
                        st = .block32;
                    } else {
                        st = .block38;
                    }
                } else {
                    st = .block32;
                }
            },
            .block32 => {
                if ((psx.fieldAt(u32, &a._bits_unk8_0, 0).* & 0x80) != 0) {
                    const img: *align(2) u16 = @alignCast(psx.fieldAt(u16, &a.unk11A4, 960));
                    c.vs_main_loadClut(img, @as(c_uint, a.unkF) +% 4, 0, psx.conv(c_uint, sp2C));
                    var_s0 = 0;
                    var clutOffset: c_int = 0xA0;
                    while (true) {
                        weapon0 = psx.at(v.g.vs_battle_wepModels(), @as(c_int, a.unkF) *% 2 +% var_s0).*;
                        if (weapon0) |w| {
                            psx.byteAt(u32, w, 8).* = (psx.byteAt(u32, w, 8).* & 0xFFFF_F0FF) |
                                (psx.fieldAt(u32, &a._bits_unk8_0, 0).* & 0xF00);
                            c.vs_main_loadClut(&w.unk500, @as(c_uint, a.unkF) +% 4, psx.conv(c_uint, clutOffset), @as(c_uint, w.nClutColors));
                        }
                        var_s0 +%= 1;
                        clutOffset +%= 0x30;
                        if (!(var_s0 < 2)) break;
                    }
                    st = .tail;
                } else {
                    st = .block38;
                }
            },
            .block38 => {
                const img: *align(2) u16 = @alignCast(psx.byteAt(u16, var_s5.?, 0x14C));
                c.vs_main_loadClut(img, 0x1F, @as(c_uint, var_s5.?.unkF) *% 0x10, 0x10);
                st = .tail;
            },
            .block60 => {
                if (var_v0 == 0) var_s0 = 0x40;
                a.unk6F2 = psx.conv(u8, var_s0);
                st = .block20;
            },
            .tail => {
                if (var_s3 == 0) {
                    const temp_v0_3: c_uint = psx.fieldAt(u32, &a._bits_unk8_0, 0).*;
                    const temp_v1_3: c_uint = (temp_v0_3 & 0xFFFF_F0FF) |
                        ((((temp_v0_3 >> 8) & 0xF) +% 1) & 0xF) << 8;
                    psx.fieldAt(u32, &a._bits_unk8_0, 0).* = temp_v1_3;
                    var_s0 = 0;
                    if ((temp_v1_3 & 0x80) != 0) {
                        while (true) {
                            weapon0 = psx.at(v.g.vs_battle_wepModels(), @as(c_int, a.unkF) *% 2 +% var_s0).*;
                            if (weapon0) |w| {
                                psx.byteAt(u32, w, 8).* = (psx.byteAt(u32, w, 8).* & 0xFFFF_F0FF) |
                                    (psx.fieldAt(u32, &a._bits_unk8_0, 0).* & 0xF00);
                            }
                            var_s0 +%= 1;
                            if (!(var_s0 < 2)) break;
                        }
                    }
                }
                return;
            },
        }
    }
}
