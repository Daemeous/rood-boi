//! Draws the battle HUD (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

const Site = *const fn (c_int, ?*anyopaque, c_int, c_int, ?*anyopaque, ?*anyopaque) callconv(.c) void;
const Anchor = *const fn (?*const anyopaque, ?*const anyopaque, c_int) callconv(.c) void;

/// The scratchpad's packet cursor word as a guest pointer (PC_CLASSIC's first and end).
fn cursor() ?*anyopaque {
    const word: *const u32 = @ptrFromInt(0x1F800000);
    return @ptrFromInt(@as(usize, word.*));
}

/// PC_CLASSIC (classic_hook.h) after a draw: the classic-site record of what it drew.
fn classic(kind: c_int, record: ?*anyopaque, x: c_int, first: ?*anyopaque) void {
    if (psx.weak(Site, "pc_classic_site")) |site| site(kind, record, x, 0, first, cursor());
}

/// `psx_get(vs_scratch.unk4) - bytes`: an OT entry below the front OT's end (void* arithmetic).
fn frontOt(bytes: usize) [*c]c_uint {
    return @ptrFromInt(@intFromPtr(psx.get(v.g.vs_scratch().unk4)) -% bytes);
}

/// `(flags >> bit) & 1` with a byte as the amount: x86 takes it modulo 32 (the C's shift is undefined past 31;
/// 0xFF marks an empty icon slot).
fn flagSet(flags: c_uint, bit: c_uint) bool {
    return ((flags >> @truncate(bit)) & 1) != 0;
}

/// orig: func_800CA2DC (src/BATTLE/BATTLE.PRG/5BF94.c)
/// The ability window's icons (func_800C9FE8), the battle-mode notice sliding in, the limb display, the HP / MP
/// / RISK bars and the status icons scrolling past; each reported to the classic site (ROOD_PC: the HUD's
/// anchors).
pub fn func_800CA2DC() callconv(.c) void {
    const actor = psx.get(v.g.vs_battle_characterState().*.?.stats);
    var ot: [*c]c_uint = frontOt(4);
    {
        const start = psx.get(v.g.vs_scratch().unk0);
        c.func_800C9FE8();
        if (psx.weak(Anchor, "pc_hw_anchor_packets")) |anchor| anchor(start, psx.get(v.g.vs_scratch().unk0), 0);
    }
    const mode_step = v.g.D_800EB9CD();
    if (v.g.D_800EB9CC().* == 1) {
        if (mode_step.* < 4) {
            v.g.D_800EBD64().* = 0;
            mode_step.* +%= 1;
        }
    } else if (mode_step.* != 0) {
        mode_step.* -%= 1;
    }
    if (mode_step.* != 0) {
        const start = psx.get(v.g.vs_scratch().unk0);
        var notice = c.pc_classic_battle_mode{ .step = mode_step.*, .slide = v.g.D_800EB9B0().*, .pulse = 0 };
        const first = cursor();
        const prim = c.vs_battle_setSprite(0x140, (((@as(c_int, mode_step.*) * 3 - 4) << 16) -% v.g.D_800EB9B0().*) | 0xEC, 0xA0049, frontOt(0xC));
        if (psx.weak(Anchor, "pc_hw_anchor_packets")) |anchor| anchor(start, psx.get(v.g.vs_scratch().unk0), 2);
        prim[1] = 0xE1000017;
        const pulse = c.rsin(v.g.D_800EBD64().* & 0x7FF);
        prim[4] = 0x373D0040;
        prim[2] +%= @bitCast(pulse >> 5);
        notice.pulse = pulse >> 5;
        classic(11, &notice, 0, first); // PC_CLASSIC_SITE_BATTLE_MODE
        v.g.D_800EBD64().* = psx.conv(u16, @as(c_int, v.g.D_800EBD64().*) +% v.g.vs_gametime_tickspeed().* *% 64);
    }
    var k: c_int = undefined;
    if (v.g.D_800EB9AF().* != 0) {
        if (v.g.D_800EB9B0().* == 0x200000) {
            return;
        }
        v.g.D_800EB9B0().* +%= 0x80000;
    } else {
        const box7 = &v.g.textBoxes()[7];
        k = box7.state;
        if ((v.g.vs_battle_lowerScreenUiState().* & 2) == 0 and (k == 0 or k != @as(c_int, box7.unk22) + 1)) {
            const first = cursor();
            const side: usize = v.g.D_800EB9CC().* & 1;
            k = 0xB80008;
            var i: usize = 0;
            while (i < 5) : (i += 1) {
                const j = i + side * 5;
                const prim = c.vs_battle_setSpriteDefaultTexPage(0, k +% v.g.D_800EBCEC()[j], v.g.D_800EBD14()[j], ot);
                const limb = &psx.get(v.g.vs_battle_characterState().*.?.stats).?.limbs[i];
                prim[2] = psx.at(v.g.D_800EBC54(), c.vs_battle_getLimbStatus(@ptrCast(limb))).* | 0x64000000;
                prim[4] = v.g.D_800EBD3C()[j];
            }
            classic(9, actor, @intCast(side), first); // PC_CLASSIC_SITE_LIMBS
        }
        if (v.g.D_800EB9B0().* != 0) {
            v.g.D_800EB9B0().* -%= 0x80000;
        }
    }
    {
        const first = cursor();
        const a = actor.?;
        c._renderStatBar(0, a.currentHP, a.maxHP);
        c._renderStatBar(1, a.currentMP, a.maxMP);
        k = a.risk;
        if (k != 0) {
            c._renderStatBar(2, k, 100);
        }
        if (k == 0) {
            c.vs_battle_renderTextRaw("RISK  ZERO", 0x16000C -% v.g.D_800EB9B0().*, frontOt(0xC));
        }
        classic(8, actor, 0, first); // PC_CLASSIC_SITE_BARS
    }
    if (v.g.D_800EB9AE().* != 0) {
        return;
    }
    const icons_state = &v.g.D_800EB9D0().u8;
    const flags: c_uint = @bitCast(c.vs_battle_getStatusFlags(@ptrCast(actor)));
    var count: c_uint = 0;
    var selected: c_int = 0xFF;
    var bit: u6 = 0;
    while (bit < 32) : (bit += 1) {
        if (((flags >> @intCast(bit)) & 1) != 0) {
            if (bit == icons_state[1]) {
                selected = @bitCast(count);
            }
            count += 1;
        }
    }
    for (0..3) |slot| {
        if (!flagSet(flags, icons_state[slot])) {
            icons_state[slot] = 0xFF;
        }
    }
    if (count == 0) {
        return;
    }
    var j: c_int = icons_state[3];
    if (j == 12) {
        icons_state[0] = icons_state[1];
        icons_state[1] = icons_state[2];
        icons_state[2] = 0xFF;
        var i: c_int = (@as(c_int, icons_state[1]) + 1) & 0xFF;
        k = 0;
        while (k < 32) : ({
            k += 1;
            i += 1;
        }) {
            i = @rem(i, 32);
            if (flagSet(flags, @intCast(i)) and i != icons_state[1]) {
                icons_state[2] = @intCast(i);
                break;
            }
        }
    }
    if (icons_state[0] == icons_state[2] and icons_state[1] != 0xFF) {
        if (count == 1 and j < 3) {
            j = 0;
        }
        if (count == 2 and icons_state[0] != 0xFF and psx.conv(c_uint, j -% 12) < 3) {
            j = 12;
        }
    }
    k = -8;
    if (j >= 9) {
        if (j < 12) {
            k = 8 - j * 2;
        } else {
            k = 24 - j * 2;
        }
    }
    j += 1;
    if (j != icons_state[3] and v.g.vs_gametime_tickspeed().* == 4) {
        j = (j + 1) & 0xE;
    }
    const right_x: c_int = k + 16;
    icons_state[3] = @intCast(j & 0xF);
    var icons = c.pc_classic_status_icons{ .previous = icons_state[0], .current = icons_state[1], .next = icons_state[2], .count = @bitCast(count), .selected = selected, .offset = k, .slide = v.g.D_800EB9B0().* };
    const first = cursor();
    draw: {
        c.func_800C9CB4(icons_state[2], right_x, 1);
        ot = @ptrFromInt(@intFromPtr(ot) -% 4);
        c.func_800C9CB4(icons_state[1], k, 1);
        c.func_800C9CB4(icons_state[1], right_x, 0);
        c.func_800C9CB4(icons_state[0], k, 0);
        k = 0x80000 -% v.g.D_800EB9B0().*;
        if (count < 3) {
            var i: c_uint = 0;
            while (i < count) : (i += 1) {
                const prim = c.vs_battle_setSpriteDefaultTexPage(0x80, @as(c_int, @bitCast(0x94 + i * 3)) | k, 0x40004, ot);
                prim[4] = 0x37F40C1A;
            }
            break :draw;
        }
        const lower: c_int = k +% 0x30000;
        if (selected != 0xFF) {
            const prim = c.vs_battle_setSprite(0x80, ((selected & 7) * 3 + 0x94) | (if ((selected & 8) == 0) k else lower), 0x40004, ot);
            prim[4] = 0x37F40C1A;
        }
        var width: c_int = 25;
        if (count < 9) {
            width = @as(c_int, @bitCast(count)) * 3 + 1;
        } else {
            const prim = c.vs_battle_setSprite(0x80, lower | 0x94, @as(c_int, @bitCast((count - 8) * 3 + 1)) | 0x40000, ot);
            prim[4] = 0x37F40C00;
        }
        const prim = c.vs_battle_setSpriteDefaultTexPage(0x80, k | 0x94, width | 0x40000, ot);
        prim[4] = 0x37F40C00;
    }
    classic(10, &icons, 0, first); // PC_CLASSIC_SITE_STATUS_ICONS
}
