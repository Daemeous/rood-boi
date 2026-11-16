//! Draws the ability window's icon rows (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
const sprite_item = @import("common").sprite_item;

const Site = *const fn (c_int, ?*anyopaque, c_int, c_int, ?*anyopaque, ?*anyopaque) callconv(.c) void;

/// The packet cursor (PC_CLASSIC's first and end).
fn cursor() ?*anyopaque {
    return v.g.vs_scratch().unk0;
}

/// PC_CLASSIC_SITE_ABILITY_ICONS after a draw: the classic-site record of what it drew.
fn classic(icons: *c.pc_classic_ability_icons, first: ?*anyopaque) void {
    if (psx.weak(Site, "pc_classic_site")) |site| site(12, icons, 0, 0, first, cursor());
}

fn menuOt() [*c]c_uint {
    return @ptrCast(@alignCast(psx.get(v.g.vs_scratch().unk8)));
}

/// orig: vs_battle_drawShortcutHud (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Steps the window's slide (back while vs_battle_shortcutHudRequested is set, else along vs_battle_animationIndices) and, unless
/// hidden (10), draws the right-hand slots' labels and icons, the centre icon, then the left-hand slots with
/// their numbers; each group reported to the classic site (ROOD_PC: the weapon slots' labels).
pub fn vs_battle_drawShortcutHud() callconv(.c) void {
    const step = v.g.vs_battle_shortcutHudAnimationStep();
    if (v.g.vs_battle_shortcutHudRequested().* != 0) {
        if (step.* != 0) {
            step.* -= 1;
        }
    } else {
        step.* = v.g.vs_battle_animationIndices()[step.*];
    }
    if (step.* != 10) {
        var offset: c_int = -@as(c_int, v.g.vs_battle_rowAnimationSteps()[step.*]);
        var icons = c.pc_classic_ability_icons{ .animation = step.*, .offset = offset, .group = 0, .availability = 0 };
        var i: usize = 0;
        while (i < 4) : (i += 1) {
            const label = c.pc_weapon_slots_label(@intCast(i + 4));
            icons.group = @intCast(i + 4);
            icons.availability = label;
            const first = cursor();
            if (label != 0) {
                c.func_800C9F98(((offset +% v.g.D_800EBC98()[i]) & 0xFFFF) | (@as(c_int, v.g.D_800EBC98()[i + 4]) << 0x10), label);
                _ = sprite_item.sprite(menuOt(), label, ((offset +% v.g.D_800EBCA0()[i]) & 0xFFFF) | (@as(c_int, v.g.D_800EBCA0()[i + 4]) << 0x10), v.g.D_800EBCA8()[i], @bitCast(@as(c_uint, 0xE1000017)), @bitCast(@as(c_uint, v.g.D_800EBCB8()[i]) | 0x373D0000));
            }
            classic(&icons, first);
        }
        icons.group = 8;
        icons.availability = 0;
        {
            const first = cursor();
            _ = sprite_item.sprite(menuOt(), 0x180, ((offset +% 0x48) & 0xFFFF) | 0x660000, 0x1E001E, sprite_item.page, 0x37FBA0B8);
            classic(&icons, first);
        }
        offset = 0xA0 -% offset;
        i = 0;
        while (i < 4) : (i += 1) {
            const label = c.pc_weapon_slots_label(@intCast(i));
            icons.group = @intCast(i);
            icons.availability = label;
            const first = cursor();
            if (label != 0) {
                c.func_800C9F98(((offset +% v.g.D_800EBCC0()[i]) & 0xFFFF) | (@as(c_int, v.g.D_800EBCC0()[i + 4]) << 0x10), label);
                _ = sprite_item.sprite(menuOt(), label, (((offset +% v.g.D_800EBCC8()[i]) -% 2) & 0xFFFF) | (@as(c_int, v.g.D_800EBCC8()[i + 4]) << 0x10), v.g.D_800EBCD0()[i], @bitCast(@as(c_uint, 0xE1000017)), @bitCast(@as(c_uint, v.g.D_800EBCE0()[i]) | 0x373D0000));
            }
            _ = sprite_item.sprite(menuOt(), 0x80, offset +% v.g.D_800EBC88()[3 - i], 0x100010, sprite_item.page, @bitCast(@as(c_uint, @intCast((3 - i) * 0x10)) | 0x37FB8000));
            classic(&icons, first);
        }
    }
    v.g.vs_battle_shortcutHudRequested().* = 0;
}
