//! Weapon model transform: the weapon's bone matrices and its drawing
//! (pc/src/game/BATTLE/BATTLE.PRG/49268.c).
const psx = @import("common").psx;
const gte = @import("common").gte;
const pad = @import("common").pad;
const v = @import("views_49268");
const c = @import("c_49268");

/// A matrix at byte offset `off` of `base`, as a C `MATRIX*`. The offsets are multiples of 4 from
/// 4-aligned bases, so the pointer is aligned; the alignment cast only states that.
fn matrixAt(base: anytype, off: isize) [*c]c.MATRIX {
    const p: *align(1) v.MATRIX = psx.byteAt(v.MATRIX, base, off);
    return psx.cptr(c.MATRIX, @as(*align(4) v.MATRIX, @alignCast(p)));
}

/// orig: vs_actors_buildWeaponBoneChain (pc/src/game/BATTLE/BATTLE.PRG/49268.c)
/// Steps a weapon's scale, rebuilds its bone matrices from its source matrix, and draws it on its actor's pass.
pub fn drawWeaponModel(weapon: ?*v.vs_battle_wepModels_t, source: ?*v.MATRIX) callconv(.c) void {
    const w = weapon.?;
    const scratch: *v.weaponTransformScratch = pad.at(v.weaponTransformScratch, 0x35C);
    const actor = v.g.vs_actors_actorRecords()[psx.getBits(w, "actorId")];

    psx.put(&scratch.model, psx.get(w.offsets));
    pad.at(v.MATRIX, 0x3C0).* = source.?.*;

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
        c.vs_core_mathScaleMatrixColumns(psx.cptr(c.MATRIX, &scratch.matrix), @ptrCast(&scratch.scale));
    }

    v.g.vs_actors_skeletonPose()[0] = scratch.matrix;
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
        const view = gte.viewOf(parent, gte.no_screen);

        const length: c_int = psx.byteAt(u16, psx.get(scratch.model).?, (parentBone << 4) +% 0x40).*;
        scratch.translation.vy = 0;
        scratch.translation.vz = 0;
        scratch.translation.vx = psx.conv(i16, -%length);
        gte.transformTo(&scratch.matrix.t, &view, &scratch.translation);

        const rotation = psx.byteAt(v.weaponRotation, w, i << 3);
        scratch.rotation.vx = rotation.rotation.vx;
        scratch.rotation.vy = rotation.rotation.vy;
        scratch.rotation.vz = rotation.rotation.vz;
        psx.fieldAt(c_int, &scratch.rotation, 0).* &= 0x0FFF0FFF;
        psx.fieldAt(c_int, &scratch.rotation.vz, 0).* &= 0xFFF;

        c.vs_core_mathBuildEulerRotationMatrixQuadrant(@ptrCast(&scratch.rotation), psx.cptr(c.MATRIX, &scratch.matrix));
        c.vs_core_mathPremultiplyRotationMatrix(
            matrixAt(v.g.vs_actors_skeletonPose(), parentOffset),
            psx.cptr(c.MATRIX, &scratch.matrix),
        );
        v.g.vs_actors_skeletonPose()[@intCast(i)] = scratch.matrix;
        psx.byteAt(v.MATRIX, w, @as(isize, (i << 5) + 32)).* = scratch.matrix;
    }

    const a = actor.?;
    if ((psx.bitsAt(c_int, &a.unk0._bits_skip, 0).* & 2) != 0) {
        if (a.unk0.position.vy >= 256) {
            if (a.unk0.slot == 0 or c.vs_event_eventPlaybackState() != 4) return;
        }
        if (c.vs_render_projectModelVertices(
            w,
            matrixAt(w, 32),
            (-%v.g.vs_main_nearClip().*) << 2,
            psx.get(w.offsets),
        ) != 0) {
            scratch.tpage = psx.conv(u16, psx.at(v.g.vs_battle_wepTextures(), w.texSlot).tpage);
            scratch.clut = psx.conv(u16, psx.at(v.g.vs_battle_wepTextures(), w.clutSlotOffset).clut0);

            const flags: c_uint = psx.bitsAt(c_uint, &a.unk0._bits_skip, 0).*;
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
                psx.fieldAt(c_int, &color, 0).* = a.unk0.unk54;
            }

            if (value != 0) {
                c.func_800B516C(v.g.D_800F2458(), w, &color, psx.get(scratch.model));
                return;
            }
            c.vs_render_drawModelFlatPolygons(v.g.D_800F2458(), w, &color, psx.get(scratch.model));
        }
    }
}
