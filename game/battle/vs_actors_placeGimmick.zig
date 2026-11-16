//! The weapon transform: the weapon model's matrix from the object's pose, its holder's anchors and the camera (pc/src/game/BATTLE/BATTLE.PRG/49268.c).
const psx = @import("common").psx;
const gte = @import("common").gte;
const pad = @import("common").pad;
const v = @import("views_49268");
const c = @import("c_49268");

fn svAt(addr: usize) *v.SVECTOR {
    return @ptrFromInt(addr);
}

fn matAt(addr: usize) *v.MATRIX {
    return @ptrFromInt(addr);
}

// gte_stlvnl: the translation vector stored to three words.
fn stlvnl(t: *[3]i32, mac: *const [3]i32) void {
    t.* = mac.*;
}

/// orig: vs_actors_placeGimmick (pc/src/game/BATTLE/BATTLE.PRG/49268.c)
/// Builds the weapon model's transform from the object's pose and camera, and stores it in the object's matrix block.
pub fn vs_actors_placeGimmick(object: ?*v.D_800F45E0_t, camera: ?*v.MATRIX) callconv(.c) void {
    const obj = object.?;
    const cam = camera.?;
    const scratch: *v.weaponTransformScratch = @ptrFromInt(pad.addr(0x35C));
    var holderRotation: v.MATRIX = undefined;
    var anchors: [2]v.SVECTOR = undefined;

    psx.put(&scratch.model, psx.get(obj.unk68));
    const view = gte.viewOf(cam, gte.no_screen);
    // The registers' MAC1-3, what gte_stlvnl stores: the last transform's.
    var mac: [3]i32 = undefined;
    scratch.translation.vx = scratch.position.vx;
    scratch.translation.vy = psx.conv(i16, @as(c_int, scratch.position.vy) +% 16);
    scratch.translation.vz = scratch.position.vz;
    gte.transformTo(&mac, &view, svAt(pad.addr(0x3A8)));
    const attachment: c_uint = @as(c_uint, psx.getBits(&obj.unk6C[8], "actorId"));

    composed: {
        if (attachment >= 4 and obj.unk12 != 0xFF) {
            const holder = psx.at(v.g.vs_actors_actorRecords(), obj.unk12).*;
            _ = c.vs_actors_anchorPoint(@as(c_int, obj.unk12), 0xF0, psx.cptr(c.SVECTOR, &anchors[0]), 1);
            _ = c.vs_actors_anchorPoint(@as(c_int, obj.unk12), 0xF1, psx.cptr(c.SVECTOR, &anchors[1]), 1);
            scratch.translation.vx = psx.conv(i16, @divTrunc(@as(c_int, anchors[0].vx) +% anchors[1].vx, 2));
            scratch.translation.vy = psx.conv(i16, @divTrunc(@as(c_int, anchors[0].vy) +% anchors[1].vy, 2));
            scratch.translation.vz = psx.conv(i16, @divTrunc(@as(c_int, anchors[0].vz) +% anchors[1].vz, 2));
            gte.transformTo(&mac, &view, svAt(pad.addr(0x3A8)));
            const pose: *v.SVECTOR = @ptrCast(&obj.unk6C[5]);
            scratch.rotation.vx = psx.conv(i16, @as(c_int, pose.vx) +% obj.unk24);
            scratch.rotation.vy = psx.conv(i16, @as(c_int, pose.vy) +% obj.unk26);
            scratch.rotation.vz = psx.conv(i16, @as(c_int, pose.vz) +% psx.conv(i16, obj.unk28));
            stlvnl(@as(*[3]i32, @ptrFromInt(pad.addr(0x3D4))), &mac);
            c.vs_core_mathBuildEulerRotationMatrixQuadrant(psx.cptr(c.SVECTOR, svAt(pad.addr(0x3E0))), psx.cptr(c.MATRIX, matAt(pad.addr(0x3C0))));
            scratch.rotation.vx = holder.?.unk0.pitch;
            scratch.rotation.vy = holder.?.unk0.facing;
            scratch.rotation.vz = holder.?.unk0.roll;
            c.vs_core_mathBuildEulerRotationMatrixQuadrant(psx.cptr(c.SVECTOR, svAt(pad.addr(0x3E0))), psx.cptr(c.MATRIX, &holderRotation));
            c.vs_core_mathPremultiplyRotationMatrix(psx.cptr(c.MATRIX, &holderRotation), psx.cptr(c.MATRIX, matAt(pad.addr(0x3C0))));
        } else if (attachment < 2) {
            if (obj.unk1A != 0) {
                scratch.rotation.vx = obj.unk24;
                scratch.rotation.vy = obj.unk26;
                scratch.rotation.vz = psx.conv(i16, obj.unk28);
                stlvnl(@as(*[3]i32, @ptrFromInt(pad.addr(0x3D4))), &mac);
                _ = c.RotMatrixYXZ_gte(psx.cptr(c.SVECTOR, svAt(pad.addr(0x3E0))), psx.cptr(c.MATRIX, matAt(pad.addr(0x3C0))));
                c.vs_core_mathPremultiplyRotationMatrix(psx.cptr(c.MATRIX, cam), psx.cptr(c.MATRIX, matAt(pad.addr(0x3C0))));
                // CompMatrixLV called through the two-argument form (m0 = m0 * m1), as the C does.
                const CompMatrixLV2 = *const fn ([*c]c.MATRIX, [*c]c.MATRIX) callconv(.c) void;
                @as(CompMatrixLV2, @ptrCast(&c.CompMatrixLV))(
                    psx.cptr(c.MATRIX, matAt(pad.addr(0x3C0))),
                    psx.cptr(c.MATRIX, @as(*v.MATRIX, @ptrCast(&obj.unk6C[4]))),
                );
                break :composed;
            }
            matAt(pad.addr(0x3C0)).* = @as(*v.MATRIX, @ptrCast(&obj.unk6C[4])).*;
            stlvnl(@as(*[3]i32, @ptrFromInt(pad.addr(0x3D4))), &mac);
        } else {
            const pose: *v.SVECTOR = @ptrCast(&obj.unk6C[5]);
            scratch.rotation.vx = pose.vx;
            scratch.rotation.vy = pose.vy;
            scratch.rotation.vz = pose.vz;
            stlvnl(&scratch.matrix.t, &mac);
            scratch.rotation.vx = psx.conv(i16, @as(c_int, scratch.rotation.vx) +% obj.unk24);
            scratch.rotation.vy = psx.conv(i16, @as(c_int, scratch.rotation.vy) +% obj.unk26);
            scratch.rotation.vz = psx.conv(i16, @as(c_int, scratch.rotation.vz) +% psx.conv(i16, obj.unk28));
            @as(*align(1) c_int, @ptrCast(&scratch.rotation)).* &= 0x0FFF0FFF;
            @as(*align(1) c_int, @ptrCast(&scratch.rotation.vz)).* &= 0xFFF;
            c.vs_core_mathBuildEulerRotationMatrixQuadrant(psx.cptr(c.SVECTOR, &scratch.rotation), psx.cptr(c.MATRIX, &scratch.matrix));
        }
        c.vs_core_mathPremultiplyRotationMatrix(psx.cptr(c.MATRIX, cam), psx.cptr(c.MATRIX, &scratch.matrix));
    }

    c.vs_core_mathScaleMatrixColumns(
        psx.cptr(c.MATRIX, &scratch.matrix),
        psx.cptr(c.VECTOR, @as(*v.VECTOR, @ptrCast(&v.g.vs_actors_skeletonPose()[63]))),
    );
    v.g.vs_actors_skeletonPose()[0] = scratch.matrix;
    @as(*v.MATRIX, @ptrCast(&obj.unk6C[0])).* = scratch.matrix;
    if (psx.fieldAt(c_int, &obj.unk2C, 0).* != 0x400040 or obj.unk30 != 64) {
        scratch.scale.vx = obj.unk2C;
        scratch.scale.vy = obj.unk2E;
        scratch.scale.vz = obj.unk30;
        scratch.scale.vx <<= 6;
        scratch.scale.vy <<= 6;
        scratch.scale.vz <<= 6;
        c.vs_core_mathScaleMatrixColumns(psx.cptr(c.MATRIX, &scratch.matrix), psx.cptr(c.VECTOR, &scratch.scale));
        v.g.vs_actors_skeletonPose()[0] = scratch.matrix;
        @as(*v.MATRIX, @ptrCast(&obj.unk6C[0])).* = scratch.matrix;
    }
    v.g.D_800F3C78().* = scratch.matrix;
    @as(*v.MATRIX, @ptrCast(&obj.unk6C[1])).* = scratch.matrix;
    if ((psx.fieldAt(c_int, &obj._bits_unk8_0, 0).* & 2) != 0) {
        if (c.vs_render_projectModelVertices(
            obj,
            psx.cptr(c.MATRIX, @as(*v.MATRIX, @ptrCast(&obj.unk6C[0]))),
            (-%v.g.vs_main_nearClip().*) << 2,
            psx.get(obj.unk68),
        ) != 0) {
            scratch.tpage = c.GetTPage(0, 0, 0x40, 0x100);
            if (obj.unk64.unk0 != 0 or ((psx.fieldAt(c_uint, &obj._bits_unk8_0, 0).* >> 8) & 0xF) >= 6) {
                scratch.clut = c.GetClut((@as(c_int, obj.unkF) << 4) +% 0x300, 0xFF);
            } else {
                scratch.clut = c.GetClut((@as(c_int, obj.unkF) << 4) +% 0x300, 0xED);
            }
            const flags: c_uint = psx.fieldAt(c_uint, &obj._bits_unk8_0, 0).*;
            var value = false;
            if ((((flags >> 8) & 0xF) -% 4) < 5) {
                if ((flags & 0xF00) != 0x600) value = true;
            }
            if (((flags >> 8) & 0xF) >= 12) value = true;
            if (value) {
                c.vs_render_drawModelGouraudPolygonsWithOverrides(v.g.D_800F2458(), obj, &obj.unk54, psx.get(scratch.model));
                return;
            }
            c.vs_render_drawModelGouraudPolygons(v.g.D_800F2458(), obj, &obj.unk54, psx.get(scratch.model));
        }
    }
}
