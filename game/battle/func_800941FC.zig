//! Floating battle labels: ticks them, draws the damage, status, chain and buff
//! labels, and removes the expired ones (src/BATTLE/BATTLE.PRG/2842C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

const SiteFn = *const fn (c_int, ?*anyopaque, c_int, c_int, ?*anyopaque, ?*anyopaque) callconv(.c) void;
const AnchorFn = *const fn (?*anyopaque, ?*anyopaque, c_int) callconv(.c) void;

/// The scratchpad word at 0x1F800000 as a guest pointer.
fn scratchPtr() ?*anyopaque {
    const word: *const u32 = @ptrFromInt(0x1F800000);
    return @ptrFromInt(@as(usize, word.*));
}

/// `*D_800F1BAC`: the label array's base.
fn labelBase() [*]v.D_800F1BAC_t {
    const raw: *align(2) anyopaque = @alignCast(v.g.D_800F1BAC().*.?);
    return @ptrCast(raw);
}

fn halfTick() c_int {
    return @divTrunc(v.g.vs_gametime_tickspeed().*, 2);
}

/// `label->unkB += vs_gametime_tickspeed / 2`.
fn advance(label: *v.D_800F1BAC_t) void {
    label.unkB = psx.conv(u8, @as(c_int, label.unkB) +% halfTick());
}

/// The `waiting:` tail of the switch.
fn waiting(label: *v.D_800F1BAC_t) void {
    advance(label);
    if (label.unkB >= 10) _ = c.func_80094AF8(psx.cptr(c.D_800F1BAC_t, label));
}

/// PC_HUD: the classic-draw site, with the scratchpad word read after the draw.
fn hudSite(kind: c_int, label: *v.D_800F1BAC_t, x: c_int, y: c_int, first: ?*anyopaque) void {
    if (psx.weak(SiteFn, "pc_classic_site")) |site| site(kind, label, x, y, first, scratchPtr());
}

/// PC_ANCHOR_PACKETS: the widescreen anchor over the packets the draw wrote.
fn anchorPackets(start: ?*anyopaque) void {
    if (psx.weak(AnchorFn, "pc_hw_anchor_packets")) |hook| {
        hook(start, scratchPtr(), @as(c_int, c.PC_ANCHOR_LEFT_EDGE));
    }
}

/// orig: func_800941FC (src/BATTLE/BATTLE.PRG/2842C.c)
/// Ticks the floating labels, draws each by kind and removes the expired ones.
pub fn func_800941FC() callconv(.c) void {
    if (v.g.D_800F2272().* > 0) {
        v.g.D_800F2272().* -%= 1;
        _ = c.func_800970BC();
    }
    if (v.g.D_800F1BA4().* == 0) {
        v.g.D_800F2278().* = psx.conv(i16, @as(c_int, v.g.D_800F2278().*) +% halfTick());
    }
    if (v.g.D_800F2278().* > 20) {
        const base = labelBase();
        v.g.D_800F2278().* = 0;
        var i: c_int = 0;
        while (i < @as(c_int, v.g.D_800F227E().*)) : (i +%= 1) {
            const label = &base[@intCast(i)];
            if (label.unk8.u8[0] == 6) {
                label.unk6 -%= 1;
                if (label.unk6 == -1) {
                    var p: c_int = 0;
                    while (p < i) : (p +%= 1) {
                        const other = &labelBase()[@intCast(p)];
                        if (other.unk8.u8[0] == 6) {
                            other.unk12 = psx.conv(i16, @as(c_int, other.unk12) +% 10);
                        }
                    }
                }
            }
        }
    }

    var removed: c_int = 0;
    const base = labelBase();
    v.g.D_800F227C().* = 0;
    var i: c_int = 0;
    while (i < @as(c_int, v.g.D_800F227E().*)) : (i +%= 1) {
        const label = &base[@intCast(i)];
        if (label.unk8.u8[0] == 0) continue;

        var position: c.SVECTOR = std.mem.zeroes(c.SVECTOR);
        var screen: [2]i16 align(4) = .{ 0, 0 };
        var p: c_int = 0;
        var flag: c_int = 0;
        var visible: c_int = 0;

        remove: {
            if (label.unk8.u8[0] < 3) {
                if (c.func_800A190C(@as(c_int, label.unk8.u8[1]), 0xFB, &position, 0) != 0) break :remove;
                if (position.vy > 0) position.vy = 0;
                position.vx = psx.conv(i16, @as(c_int, position.vx) +% label.unk0);
                position.vy = psx.conv(i16, @as(c_int, position.vy) +% label.unk2);
                position.vz = psx.conv(i16, @as(c_int, position.vz) +% label.unk4);
                v.g.pc_gte_anchored().* +%= 1;
                visible = c.RotTransPers(&position, @ptrCast(&screen), &p, &flag);
                v.g.pc_gte_anchored().* -%= 1;
                if (screen[0] < 32) screen[0] = 32;
                if (screen[0] > 288) screen[0] = 288;
                if (screen[1] < 40) screen[1] = 40;
                if (screen[1] > 200) screen[1] = 200;
            } else if (label.unk8.u8[0] == 3) {
                v.g.pc_gte_anchored().* +%= 1;
                visible = c.RotTransPers(@ptrCast(label), @ptrCast(&screen), &p, &flag);
                v.g.pc_gte_anchored().* -%= 1;
            } else {
                visible = 1;
            }
            if (visible <= 0) break :remove;

            sw: switch (label.unk8.u8[0]) {
                1 => {
                    if (label.unk6 <= 0) {
                        label.unkE &= 0x7F;
                        screen[1] = psx.conv(i16, @as(c_int, screen[1]) +% 8);
                        _ = c.func_80094844(&screen, psx.cptr(c.D_800F1BAC_t, label));
                        if (label.unkE == 2) {
                            _ = c.func_8009506C(@as(c_int, screen[0]), @as(c_int, screen[1]), psx.cptr(c.D_800F1BAC_t, label));
                        } else {
                            const first = scratchPtr();
                            _ = c.func_80094B0C(@as(c_int, screen[0]), @as(c_int, screen[1]), psx.cptr(c.D_800F1BAC_t, label));
                            hudSite(@as(c_int, c.PC_HUD_DAMAGE), label, @as(c_int, screen[0]), @as(c_int, screen[1]), first);
                        }
                        advance(label);
                        if (label.unkB >= 10) label.unk6 = -1;
                        if (label.unkB < 31) break :sw;
                        break :remove;
                    }
                    screen[1] = psx.conv(i16, @as(c_int, screen[1]) +% 8);
                    _ = c.func_80094844(&screen, psx.cptr(c.D_800F1BAC_t, label));
                    if ((label.unkE & 0x80) != 0) {
                        label.unkE &= 0x7F;
                        _ = c.func_80094AF8(psx.cptr(c.D_800F1BAC_t, label));
                        break :sw;
                    }
                    waiting(label);
                },
                2 => {
                    if (label.unk6 > 0) {
                        waiting(label);
                        break :sw;
                    }
                    screen[1] = psx.conv(i16, @as(c_int, screen[1]) -% @as(c_int, label.displayTextLen));
                    if (label.unk8.u8[1] != 0 or v.g.D_800F227A().* == 0) {
                        if (label.unkE == 2) {
                            _ = c.func_8009506C(@as(c_int, screen[0]), @as(c_int, screen[1]), psx.cptr(c.D_800F1BAC_t, label));
                        } else {
                            _ = c.func_80095258(@as(c_int, screen[0]), @as(c_int, screen[1]), psx.cptr(c.D_800F1BAC_t, label));
                        }
                    }
                    advance(label);
                    if (label.unkB >= 10) label.unk6 = -1;
                    if (label.unkB >= 31) {
                        label.unk8.u8[0] = 0;
                        removed +%= 1;
                    }
                },
                3 => {
                    _ = c.func_8009506C(@as(c_int, screen[0]), @as(c_int, screen[1]), psx.cptr(c.D_800F1BAC_t, label));
                    advance(label);
                    if (label.unkB >= 31) {
                        label.unk8.u8[0] = 0;
                        removed +%= 1;
                    }
                },
                4 => {
                    const first = scratchPtr();
                    const start = scratchPtr();
                    _ = c.func_800958A4(44, 48, psx.cptr(c.D_800F1BAC_t, label));
                    anchorPackets(start);
                    hudSite(@as(c_int, c.PC_HUD_STATUS), label, 44, 48, first);
                    advance(label);
                    if (label.unkB >= 46) {
                        label.unk8.u8[0] = 0;
                        removed +%= 1;
                    }
                },
                5 => {
                    const first = scratchPtr();
                    const start = scratchPtr();
                    _ = c.func_80095A4C(44, 64, psx.cptr(c.D_800F1BAC_t, label));
                    anchorPackets(start);
                    hudSite(@as(c_int, c.PC_HUD_CHAIN), label, 44, 64, first);
                    advance(label);
                    if (label.unkB >= 91) {
                        label.unk8.u8[0] = 0;
                        removed +%= 1;
                    }
                },
                6 => {
                    if (label.unk6 >= 0) break :sw;
                    const first = scratchPtr();
                    const start = scratchPtr();
                    _ = c.func_8009695C(8, 172, psx.cptr(c.D_800F1BAC_t, label));
                    anchorPackets(start);
                    hudSite(@as(c_int, c.PC_HUD_BUFF), label, 8, 172, first);
                    if (v.g.D_800F1BA4().* != 0) break :sw;
                    advance(label);
                    if (label.unkB >= 61) {
                        label.unk8.u8[0] = 0;
                        removed +%= 1;
                    }
                },
                0, 7 => {},
                else => {},
            }
            continue;
        }
        label.unk8.u8[0] = 0;
        removed +%= 1;
    }
    if (removed > 0) _ = c.func_8009639C(removed);
}
