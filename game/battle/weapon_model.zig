//! Weapon model transform: the weapon's bone matrices and its drawing
//! (src/BATTLE/BATTLE.PRG/49268.c).
const psx = @import("common").psx;
const v = @import("views_49268");
const c = @import("c_49268");

/// A matrix at byte offset `off` of `base`, as a C `MATRIX*`. The offsets are multiples of 4 from
/// 4-aligned bases, so the pointer is aligned; the alignment cast only states that.
fn matrixAt(base: anytype, off: isize) [*c]c.MATRIX {
    const p: *align(1) v.MATRIX = psx.byteAt(v.MATRIX, base, off);
    return psx.cptr(c.MATRIX, @as(*align(4) v.MATRIX, @alignCast(p)));
}

/// orig: func_800B217C (src/BATTLE/BATTLE.PRG/49268.c)
/// Steps a weapon's scale, rebuilds its bone matrices from its source matrix, and draws it on its actor's pass.
pub fn drawWeaponModel(weapon: ?*v.vs_battle_wepModels_t, source: ?*v.MATRIX) callconv(.c) void {
    const w = weapon.?;
    const scratch: *v.weaponTransformScratch = @ptrFromInt(0x1F80035C);
    const actor = v.g.D_800F4538()[psx.getBits(w, "actorId")];

    psx.put(&scratch.model, psx.get(w.offsets));
    @as(*v.MATRIX, @ptrFromInt(0x1F8003C0)).* = source.?.*;

    if (w.unk13 != 0) {
        const target: c_int = w.unk12;
        const current: c_int = w.unk11;
        var value: c_int = target -% current;
        value = @divTrunc(value, @as(c_int, w.unk13));
        w.unk11 = psx.conv(u8, current +% value);
        w.unk13 -%= 1;
        // *(int*)&weapon->clutSlotOffset: the bytes clutSlotOffset, unk11, unk12, unk13.
        const clutWord: *align(1) c_int = @ptrCast(&w.clutSlotOffset);
        if ((clutWord.* & @as(c_int, @bitCast(@as(u32, 0xFF00FF00)))) == 0) {
            psx.setBits(w, "unk8_4", 0);
        }
    }

    if (w.unk11 != 64) {
        var value: c_int = w.unk11;
        value <<= 6;
        scratch.scale.vx = value;
        scratch.scale.vy = value;
        scratch.scale.vz = value;
        c.func_800B396C(psx.cptr(c.MATRIX, &scratch.matrix), @ptrCast(&scratch.scale));
    }

    v.g.D_800F3C58()[0] = scratch.matrix;
    psx.byteAt(v.MATRIX, w, 32).* = scratch.matrix;
    scratch.translation.vx = 0;
    scratch.translation.vy = 0;

    var i: c_int = 1;
    while (i < @as(c_int, w.nBones)) : (i +%= 1) {
        const model_bone_off: c_int = (i << 4) +% 0x44;
        const parentBone: c_int = psx.at(psx.get(scratch.model).?, model_bone_off).*;
        const parentOffset: c_int = parentBone << 5;
        psx.put(&scratch.parent, psx.byteAt(v.MATRIX, w, @as(isize, parentOffset +% 0x20)));

        const parent = psx.get(scratch.parent).?;
        c.CTC2(psx.byteAt(c_uint, parent, 0).*, 0);
        c.CTC2(psx.byteAt(c_uint, parent, 4).*, 1);
        c.CTC2(psx.byteAt(c_uint, parent, 8).*, 2);
        c.CTC2(psx.byteAt(c_uint, parent, 12).*, 3);
        c.CTC2(psx.byteAt(c_uint, parent, 16).*, 4);
        c.CTC2(psx.byteAt(c_uint, parent, 20).*, 5);
        c.CTC2(psx.byteAt(c_uint, parent, 24).*, 6);
        c.CTC2(psx.byteAt(c_uint, parent, 28).*, 7);

        const length: c_int = psx.byteAt(u16, psx.get(scratch.model).?, (parentBone << 4) +% 0x40).*;
        scratch.translation.vy = 0;
        scratch.translation.vz = 0;
        scratch.translation.vx = psx.conv(i16, -%length);
        c.MTC2(psx.byteAt(c_uint, &scratch.translation, 0).*, 0);
        c.MTC2(psx.byteAt(c_uint, &scratch.translation, 4).*, 1);
        _ = c.doCOP2(0x0480012);

        const rotation = psx.byteAt(v.weaponRotation, w, i << 3);
        scratch.rotation.vx = rotation.rotation.vx;
        scratch.rotation.vy = rotation.rotation.vy;
        scratch.rotation.vz = rotation.rotation.vz;
        psx.byteAt(c_uint, &scratch.matrix.t, 0).* = c.MFC2(25);
        psx.byteAt(c_uint, &scratch.matrix.t, 4).* = c.MFC2(26);
        psx.byteAt(c_uint, &scratch.matrix.t, 8).* = c.MFC2(27);
        psx.byteAt(c_int, &scratch.rotation, 0).* &= 0x0FFF0FFF;
        psx.byteAt(c_int, &scratch.rotation.vz, 0).* &= 0xFFF;

        c.func_800B17F0(@ptrCast(&scratch.rotation), psx.cptr(c.MATRIX, &scratch.matrix));
        c.func_800B196C(
            matrixAt(v.g.D_800F3C58(), parentOffset),
            psx.cptr(c.MATRIX, &scratch.matrix),
        );
        v.g.D_800F3C58()[@intCast(i)] = scratch.matrix;
        psx.byteAt(v.MATRIX, w, @as(isize, (i << 5) + 32)).* = scratch.matrix;
    }

    const a = actor.?;
    if ((psx.byteAt(c_int, a, 8).* & 2) != 0) {
        if (psx.byteAt(i16, a, 0x1E).* >= 256) {
            if (a.unk0.unkF == 0 or c.func_800BEB00() != 4) return;
        }
        if (c.func_800B3A68(
            w,
            matrixAt(w, 32),
            (-%v.g.vs_main_nearClip().*) << 2,
            psx.get(w.offsets),
        ) != 0) {
            scratch.tpage = psx.conv(u16, psx.at(v.g.vs_battle_wepTextures(), w.texSlot).tpage);
            scratch.clut = psx.conv(u16, psx.at(v.g.vs_battle_wepTextures(), w.clutSlotOffset).clut0);

            const flags: c_uint = psx.byteAt(c_uint, a, 8).*;
            var value: c_int = 0;
            if ((((flags >> 8) & 15) -% 4) < 5) {
                value = 1;
                if ((flags & 0xF00) == 0x600) value = 0;
            }
            if ((flags & 0xF00) == 0xF00) value = 1;

            // color.cd is uninitialised in the first branch; the port zeroes it.
            var color: v.CVECTOR = .{ .r = 0, .g = 0, .b = 0, .cd = 0 };
            if (psx.getBits(w, "unk8_6") != 0) {
                color.r = w.unk5C0;
                color.g = w.unk5C0;
                color.b = w.unk5C0;
            } else {
                psx.byteAt(c_int, &color, 0).* = psx.byteAt(c_int, a, 0x54).*;
            }

            if (value != 0) {
                c.func_800B516C(v.g.D_800F2458(), w, &color, psx.get(scratch.model));
                return;
            }
            c.func_800B3CF0(v.g.D_800F2458(), w, &color, psx.get(scratch.model));
        }
    }
}
