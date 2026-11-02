//! Draws the ability window's icon rows (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

const Site = *const fn (c_int, ?*anyopaque, c_int, c_int, ?*anyopaque, ?*anyopaque) callconv(.c) void;

/// The scratchpad's packet cursor word as a guest pointer (PC_CLASSIC's first and end).
fn cursor() ?*anyopaque {
    const word: *const u32 = @ptrFromInt(0x1F800000);
    return @ptrFromInt(@as(usize, word.*));
}

/// PC_CLASSIC_SITE_ABILITY_ICONS after a draw: the classic-site record of what it drew.
fn classic(icons: *c.pc_classic_ability_icons, first: ?*anyopaque) void {
    if (psx.weak(Site, "pc_classic_site")) |site| site(12, icons, 0, 0, first, cursor());
}

fn menuOt() [*c]c_uint {
    return @ptrCast(@alignCast(psx.get(v.g.vs_scratch().unk8)));
}

/// orig: func_800C9FE8 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Steps the window's slide (back while D_800EBC78 is set, else along vs_battle_animationIndices) and, unless
/// hidden (10), draws the right-hand slots' labels and icons, the centre icon, then the left-hand slots with
/// their numbers; each group reported to the classic site (ROOD_PC: the weapon slots' labels).
pub fn func_800C9FE8() callconv(.c) void {
    const step = v.g.D_800EBCE8();
    if (v.g.D_800EBC78().* != 0) {
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
                const prim = c.vs_battle_setSpriteDefaultTexPage(label, ((offset +% v.g.D_800EBCA0()[i]) & 0xFFFF) | (@as(c_int, v.g.D_800EBCA0()[i + 4]) << 0x10), v.g.D_800EBCA8()[i], menuOt());
                prim[1] = 0xE1000017;
                prim[4] = @as(c_uint, v.g.D_800EBCB8()[i]) | 0x373D0000;
            }
            classic(&icons, first);
        }
        icons.group = 8;
        icons.availability = 0;
        {
            const first = cursor();
            c.vs_battle_setSpriteDefaultTexPage(0x180, ((offset +% 0x48) & 0xFFFF) | 0x660000, 0x1E001E, menuOt())[4] = 0x37FBA0B8;
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
                const prim = c.vs_battle_setSpriteDefaultTexPage(label, (((offset +% v.g.D_800EBCC8()[i]) -% 2) & 0xFFFF) | (@as(c_int, v.g.D_800EBCC8()[i + 4]) << 0x10), v.g.D_800EBCD0()[i], menuOt());
                prim[1] = 0xE1000017;
                prim[4] = @as(c_uint, v.g.D_800EBCE0()[i]) | 0x373D0000;
            }
            c.vs_battle_setSpriteDefaultTexPage(0x80, offset +% v.g.D_800EBC88()[3 - i], 0x100010, menuOt())[4] = @as(c_uint, @intCast((3 - i) * 0x10)) | 0x37FB8000;
            classic(&icons, first);
        }
    }
    v.g.D_800EBC78().* = 0;
}
