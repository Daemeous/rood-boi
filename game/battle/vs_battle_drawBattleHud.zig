//! Draws the battle HUD (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
const sprite_item = common.sprite_item;

const Site = *const fn (c_int, ?*anyopaque, c_int, c_int, ?*anyopaque, ?*anyopaque) callconv(.c) void;
const Anchor = *const fn (?*const anyopaque, ?*const anyopaque, c_int) callconv(.c) void;

/// The packet cursor (PC_CLASSIC's first and end).
fn cursor() ?*anyopaque {
    return v.g.vs_scratch().unk0;
}

/// PC_CLASSIC (classic_hook.h) after a draw: the classic-site record of what it drew.
fn classic(kind: c_int, record: ?*anyopaque, x: c_int, first: ?*anyopaque) void {
    if (psx.weak(Site, "pc_classic_site")) |site| site(kind, record, x, 0, first, cursor());
}

/// `vs_scratch.unk4 - bytes`: an OT entry below the front OT's end (void* arithmetic).
fn frontOt(bytes: usize) [*c]c_uint {
    return @ptrFromInt(@intFromPtr(psx.get(v.g.vs_scratch().unk4)) -% bytes);
}

/// `(flags >> bit) & 1` with a byte as the amount: x86 takes it modulo 32 (the C's shift is undefined past 31;
/// 0xFF marks an empty icon slot).
fn flagSet(flags: c_uint, bit: c_uint) bool {
    return ((flags >> @truncate(bit)) & 1) != 0;
}

/// orig: vs_battle_drawBattleHud (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// The ability window's icons (vs_battle_drawShortcutHud), the battle-mode notice sliding in, the limb display, the HP / MP
/// / RISK bars and the status icons scrolling past; each reported to the classic site (ROOD_PC: the HUD's
/// anchors).
pub fn vs_battle_drawBattleHud() callconv(.c) void {
    const actor = psx.get(v.g.vs_battle_characterState().*.?.stats);
    var ot: [*c]c_uint = frontOt(4);
    {
        const start = psx.get(v.g.vs_scratch().unk0);
        c.vs_battle_drawShortcutHud();
        if (psx.weak(Anchor, "pc_hw_anchor_packets")) |anchor| anchor(start, psx.get(v.g.vs_scratch().unk0), 0);
    }
    const mode_step = v.g.vs_battle_battleModeIndicatorStep();
    if (v.g.vs_battle_hudBattleMode().* == 1) {
        if (mode_step.* < 4) {
            v.g.vs_battle_battleModeIndicatorWobble().* = 0;
            mode_step.* +%= 1;
        }
    } else if (mode_step.* != 0) {
        mode_step.* -%= 1;
    }
    if (mode_step.* != 0) {
        const start = psx.get(v.g.vs_scratch().unk0);
        var notice = c.pc_classic_battle_mode{ .step = mode_step.*, .slide = v.g.vs_battle_hudSlideOffset().*, .pulse = 0 };
        const first = cursor();
        const pulse = c.rsin(v.g.vs_battle_battleModeIndicatorWobble().* & 0x7FF);
        // The sprite (brightness 0x140: grey 0x40, semi-transparent) in its own draw mode, its colour word raised by the pulse.
        const item = sprite_item.sprite(frontOt(0xC), 0x140, (((@as(c_int, mode_step.*) * 3 - 4) << 16) -% v.g.vs_battle_hudSlideOffset().*) | 0xEC, 0xA0049, @bitCast(@as(c_uint, 0xE1000017)), 0x373D0040);
        sprite_item.color(item, @as(c_int, 0x404040) +% (pulse >> 5), true);
        if (psx.weak(Anchor, "pc_hw_anchor_packets")) |anchor| anchor(start, psx.get(v.g.vs_scratch().unk0), 2);
        notice.pulse = pulse >> 5;
        classic(11, &notice, 0, first); // PC_CLASSIC_SITE_BATTLE_MODE
        v.g.vs_battle_battleModeIndicatorWobble().* = psx.conv(u16, @as(c_int, v.g.vs_battle_battleModeIndicatorWobble().*) +% v.g.vs_gametime_tickspeed().* *% 64);
    }
    var k: c_int = undefined;
    if (v.g.vs_battle_hudHidden().* != 0) {
        if (v.g.vs_battle_hudSlideOffset().* == 0x200000) {
            return;
        }
        v.g.vs_battle_hudSlideOffset().* +%= 0x80000;
    } else {
        const box7 = &v.g.textBoxes()[7];
        k = box7.state;
        if ((v.g.vs_battle_lowerScreenUiState().* & 2) == 0 and (k == 0 or k != @as(c_int, box7.unk22) + 1)) {
            const first = cursor();
            const side: usize = v.g.vs_battle_hudBattleMode().* & 1;
            k = 0xB80008;
            var i: usize = 0;
            while (i < 5) : (i += 1) {
                const j = i + side * 5;
                const limb = &psx.get(v.g.vs_battle_characterState().*.?.stats).?.limbs[i];
                const item = sprite_item.sprite(ot, 0, k +% v.g.D_800EBCEC()[j], v.g.D_800EBD14()[j], sprite_item.page, @bitCast(v.g.D_800EBD3C()[j]));
                // (the status colour word: its bit 25, when set, makes the sprite semi-transparent)
                const word: c_uint = @as(c_uint, @bitCast(psx.at(v.g.vs_battle_limbStatusColors(), c.vs_battle_getLimbStatus(@ptrCast(limb))).*));
                sprite_item.color(item, @bitCast(word & 0xFFFFFF), word & 0x02000000 != 0);
            }
            classic(9, actor, @intCast(side), first); // PC_CLASSIC_SITE_LIMBS
        }
        if (v.g.vs_battle_hudSlideOffset().* != 0) {
            v.g.vs_battle_hudSlideOffset().* -%= 0x80000;
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
            c.vs_battle_renderTextRaw("RISK  ZERO", 0x16000C -% v.g.vs_battle_hudSlideOffset().*, frontOt(0xC));
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
    var icons = c.pc_classic_status_icons{ .previous = icons_state[0], .current = icons_state[1], .next = icons_state[2], .count = @bitCast(count), .selected = selected, .offset = k, .slide = v.g.vs_battle_hudSlideOffset().* };
    const first = cursor();
    draw: {
        c.vs_battle_drawSlidingStatusIcon(icons_state[2], right_x, 1);
        ot = @ptrFromInt(@intFromPtr(ot) -% 4);
        c.vs_battle_drawSlidingStatusIcon(icons_state[1], k, 1);
        c.vs_battle_drawSlidingStatusIcon(icons_state[1], right_x, 0);
        c.vs_battle_drawSlidingStatusIcon(icons_state[0], k, 0);
        k = 0x80000 -% v.g.vs_battle_hudSlideOffset().*;
        if (count < 3) {
            var i: c_uint = 0;
            while (i < count) : (i += 1) {
                _ = sprite_item.sprite(ot, 0x80, @as(c_int, @bitCast(0x94 + i * 3)) | k, 0x40004, sprite_item.page, 0x37F40C1A);
            }
            break :draw;
        }
        const lower: c_int = k +% 0x30000;
        if (selected != 0xFF) {
            _ = sprite_item.sprite(ot, 0x80, ((selected & 7) * 3 + 0x94) | (if ((selected & 8) == 0) k else lower), 0x40004, 0, 0x37F40C1A);
        }
        var width: c_int = 25;
        if (count < 9) {
            width = @as(c_int, @bitCast(count)) * 3 + 1;
        } else {
            _ = sprite_item.sprite(ot, 0x80, lower | 0x94, @as(c_int, @bitCast((count - 8) * 3 + 1)) | 0x40000, 0, 0x37F40C00);
        }
        _ = sprite_item.sprite(ot, 0x80, k | 0x94, width | 0x40000, sprite_item.page, 0x37F40C00);
    }
    classic(10, &icons, 0, first); // PC_CLASSIC_SITE_STATUS_ICONS
}
