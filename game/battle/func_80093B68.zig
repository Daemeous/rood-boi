//! Queues a damage-number entry over an actor's limb (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80093B68 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Queues the number `value` (low 15 bits, capped at 999; bit 31 kept as bit 15) at `actor`'s limb `limbIndex`
/// (-1: the actor itself), stacked over its earlier entries; bit 7 of `flags` skips actors off screen.
/// Returns 0, or -2 when the limb or a position cannot be read.
pub fn func_80093B68(actor: c_int, limbIndex: c_int, value: c_int, flags: c_int) callconv(.c) c_int {
    var position: v.SVECTOR = undefined;
    var limb: c_int = undefined;
    if (limbIndex != -1) {
        limb = c.func_800A152C(actor, limbIndex, 2);
        if (limb < 0) return -2;
    } else {
        limb = 255;
    }

    var count: c_int = 0;
    var frame: c_int = 0;
    var i: c_int = 0;
    while (i < @as(c_int, v.g.D_800F227E().*)) : (i +%= 1) {
        const e = psx.at(queue(), i);
        if (e.unk8.u8[0] == 1 and @as(c_int, e.unk8.u8[1]) == actor and e.unk6 >= 0) {
            frame = e.unkB;
            count +%= 1;
        }
    }
    var stacked: c_int = 0;
    i = 0;
    while (i < @as(c_int, v.g.D_800F227E().*)) : (i +%= 1) {
        const e = psx.at(queue(), i);
        if (e.unk8.u8[0] == 1 and @as(c_int, e.unk8.u8[1]) == actor and @as(c_int, e.unkA) == limb and
            @as(c_int, e.unk6) == count -% 1)
        {
            stacked = 128;
            break;
        }
    }

    const p: *v.D_800F1BAC_t = @ptrCast(@alignCast(c.func_800962E4()));
    const result = if (limb != 255)
        c.func_800A1AF8(actor, limb, @ptrCast(p), 2)
    else
        c.func_800A190C(actor, 255, @ptrCast(p), 2);
    if (result != 0) return -2;

    var mode = flags;
    if (mode & 128 != 0) {
        if (c.func_800A1280(actor, 251, @ptrCast(&position), 0) != 0) return 0;
        if (@as(u16, @bitCast(position.vx)) >= 321) return 0;
        if (@as(u16, @bitCast(position.vy)) >= 241) return 0;
    }
    mode &= 127;
    if (c.func_800A190C(actor, 251, @ptrCast(&position), 0) != 0) return -2;

    p.unk10 = 4096;
    p.unk12 = 4096;
    p.unk0 = psx.conv(i16, @as(c_int, p.unk0) -% position.vx);
    p.unk2 = psx.conv(i16, @as(c_int, p.unk2) -% position.vy);
    p.unk4 = psx.conv(i16, @as(c_int, p.unk4) -% position.vz);
    p.unk8.u8[0] = 1;
    p.unk6 = psx.conv(i16, count);
    p.unkB = psx.conv(u8, frame);
    p.unk8.u8[1] = psx.conv(u8, actor);
    p.unkA = psx.conv(u8, limb);
    p.unkE = psx.conv(u8, mode | stacked);
    const amount = value & 0x7FFF;
    const sign: c_uint = (psx.conv(c_uint, value) >> 16) & 0x8000;
    if (amount > 999) {
        p.unkC = psx.conv(u16, @as(c_uint, 999) | sign);
    } else {
        p.unkC = psx.conv(u16, psx.conv(c_uint, amount) | sign);
    }
    _ = c.sprintf(&p.displayText, v.g.D_800691E4(), @as(c_int, p.unkC & 0x7FFF));
    p.displayTextLen = psx.conv(u8, c.strlen(&p.displayText));
    v.g.D_800F227E().* +%= 1;
    return 0;
}

/// *D_800F1BAC as its entries (the views give the pointer opaque), read each time as the C does.
inline fn queue() [*c]v.D_800F1BAC_t {
    return @ptrCast(@alignCast(v.g.D_800F1BAC().*));
}
