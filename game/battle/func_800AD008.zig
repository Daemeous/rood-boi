//! Per-frame pose setup for a battle actor: resets its pose and loads the current animation frame's keys (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AD008 (src/BATTLE/BATTLE.PRG/40564.c)
/// Resets an actor's pose and loads its animation's keyframes for the current frame into the pose.
pub fn func_800AD008(arg0: ?*anyopaque, arg1: ?*anyopaque) callconv(.c) void {
    const actor: *v.D_800F4538_t = @ptrCast(@alignCast(arg0.?));
    const pose: *v.D_800F4538_unkC54 = @ptrCast(@alignCast(arg1.?));

    if (psx.getBits(actor, "unk5AC_23") != 0) {
        c.func_800AE47C(@ptrCast(actor));
    }
    psx.setBits(actor, "unk5AC_23", 0);
    pose.unk540 = 0;
    psx.byteAt(u8, &pose.unk544, 5).* = 0;
    pose.unk150[41].vx = 0;
    pose.unk150[41].vy = 0;
    pose.unk150[41].vz = 0;
    pose.unk150[41].pad = 0;
    pose.unk3F0[41].pad = 0;

    var headerC: [*c]c_ushort = null;
    const ret = c.func_800AD494(actor, @as(c_int, actor.animationId), &headerC);
    if (ret == 0) return;
    const anim: *align(1) v.func_800AD494_t = @ptrFromInt(@as(usize, psx.conv(u32, ret)));
    var header: [*]align(1) c_ushort = @ptrCast(headerC);

    if (header[2] == 0) {
        pose.unk542 = 0xFFFF;
    } else {
        var data: *align(1) u8 = @ptrFromInt(@as(usize, psx.conv(u32, anim.unk0)));
        data = psx.byteAt(u8, data, @as(isize, header[2]));
        var value: c_int = data.*;
        if (value == 0xFF) {
            value = @as(c_int, psx.byteAt(u8, data, 1).*) +% 0xFF;
            data = psx.byteAt(u8, data, 2);
        } else {
            data = psx.byteAt(u8, data, 1);
        }
        pose.unk542 = psx.conv(u16, value);
        psx.put(&pose.unk544, data);
    }
    psx.setBits(pose, "unk548_16", 0);
    psx.byteAt(u8, &pose.unk544, 4).* = actor.animationId;

    if (header[4] != 0) {
        const data: *align(1) u8 = @ptrFromInt(@as(usize, psx.conv(u32, anim.unk0) +% psx.conv(u32, header[4])));
        c.func_800ACFA0(psx.cptr(c.SVECTOR, &pose.unk150[41]), @ptrCast(data), 0xFF);
    }

    const frame: c_int = psx.byteAt(u8, header, 2).*;
    if (frame != 0xFF) {
        const stride: c_int = (@as(c_int, actor.unk0.nBones) *% 4 +% 10) *% frame +% 8;
        header = @ptrCast(psx.byteAt(c_ushort, anim, @as(isize, stride)));
    }

    {
        const data: *align(1) u8 = @ptrFromInt(@as(usize, psx.conv(u32, anim.unk0) +% psx.conv(u32, header[3])));
        c.func_800ACFA0(psx.cptr(c.SVECTOR, &pose.unk0[41]), @ptrCast(data), frame);
    }

    var i: c_int = 0;
    while (i < actor.unk0.nBones) : (i +%= 1) {
        const data: *align(1) u8 = @ptrFromInt(@as(usize, psx.conv(u32, anim.unk0) +% psx.conv(u32, header[@as(usize, @intCast(5 +% i))])));
        c.func_800ACFA0(psx.cptr(c.SVECTOR, psx.at(&pose.unk0, i)), @ptrCast(data), frame);
    }

    if ((psx.byteAt(u8, header, 3).* & 1) != 0) {
        i = 0;
        while (i < actor.unk0.nBones) : (i +%= 1) {
            const hi: usize = @intCast(@as(c_int, actor.unk0.nBones) +% i +% 5);
            var data: *align(1) u8 = @ptrFromInt(@as(usize, psx.conv(u32, anim.unk0) +% psx.conv(u32, header[hi])));
            const sv = psx.at(&pose.unk150, i);
            if (data.* == 0xFE and psx.byteAt(u8, data, 1).* == 1) {
                data = psx.byteAt(u8, data, 2);
                c.func_800ACFA0(psx.cptr(c.SVECTOR, sv), @ptrCast(data), frame);
                sv.pad = 0;
                sv.vx >>= 1;
                sv.vy >>= 1;
                sv.vz >>= 1;
                if (frame == 0xFF) sv.pad = 8;
            } else {
                sv.vx = data.*;
                sv.vy = psx.byteAt(u8, data, 1).*;
                sv.vz = psx.byteAt(u8, data, 2).*;
                sv.pad = 0;
                if (frame == 0xFF) sv.pad = 3;
            }
            psx.at(&pose.unk3F0, i).pad = 0;
        }
    } else {
        i = 0;
        while (i < actor.unk0.nBones) : (i +%= 1) {
            const sv = psx.at(&pose.unk150, i);
            sv.vx = 0x40;
            sv.vy = 0x40;
            sv.vz = 0x40;
            sv.pad = 0;
            psx.at(&pose.unk3F0, i).pad = 0;
        }
    }
}
