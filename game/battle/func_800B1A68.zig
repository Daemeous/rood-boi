//! The weapon transform: the weapon model's matrix from the object's pose, its holder's anchors and the camera (src/BATTLE/BATTLE.PRG/49268.c).
const psx = @import("common").psx;
const v = @import("views_49268");
const c = @import("c_49268");

fn svAt(comptime addr: usize) *v.SVECTOR {
    return @ptrFromInt(addr);
}

fn matAt(comptime addr: usize) *v.MATRIX {
    return @ptrFromInt(addr);
}

// gte_ldv0 on a scratchpad SVECTOR: VXY0 and VZ0 from two words.
fn ldv0(addr: usize) void {
    c.MTC2(@as(*const c_uint, @ptrFromInt(addr)).*, 0);
    c.MTC2(@as(*const c_uint, @ptrFromInt(addr + 4)).*, 1);
}

// gte_rtv0tr2.
fn rtv0tr2() void {
    _ = c.doCOP2(0x0480012);
}

// gte_stlvnl: the translation vector stored to three words.
fn stlvnl(t: *[3]i32) void {
    t[0] = @bitCast(c.MFC2(25));
    t[1] = @bitCast(c.MFC2(26));
    t[2] = @bitCast(c.MFC2(27));
}

// gte_SetRotMatrix.
fn setRotMatrix(m: *v.MATRIX) void {
    c.CTC2(psx.byteAt(c_uint, m, 0).*, 0);
    c.CTC2(psx.byteAt(c_uint, m, 4).*, 1);
    c.CTC2(psx.byteAt(c_uint, m, 8).*, 2);
    c.CTC2(psx.byteAt(c_uint, m, 12).*, 3);
    c.CTC2(psx.byteAt(c_uint, m, 16).*, 4);
}

// gte_SetTransMatrix.
fn setTransMatrix(m: *v.MATRIX) void {
    c.CTC2(psx.byteAt(c_uint, m, 20).*, 5);
    c.CTC2(psx.byteAt(c_uint, m, 24).*, 6);
    c.CTC2(psx.byteAt(c_uint, m, 28).*, 7);
}

/// orig: func_800B1A68 (src/BATTLE/BATTLE.PRG/49268.c)
/// Builds the weapon model's transform from the object's pose and camera, and stores it in the object's matrix block.
pub fn func_800B1A68(object: ?*v.D_800F45E0_t, camera: ?*v.MATRIX) callconv(.c) void {
    const obj = object.?;
    const cam = camera.?;
    const scratch: *v.weaponTransformScratch = @ptrFromInt(0x1F80035C);
    var holderRotation: v.MATRIX = undefined;
    var anchors: [2]v.SVECTOR = undefined;

    psx.put(&scratch.model, psx.get(obj.unk68));
    setRotMatrix(cam);
    setTransMatrix(cam);
    scratch.translation.vx = scratch.position.vx;
    scratch.translation.vy = psx.conv(i16, @as(c_int, scratch.position.vy) +% 16);
    scratch.translation.vz = scratch.position.vz;
    ldv0(0x1F8003A8);
    rtv0tr2();
    const attachment: c_uint = @as(c_uint, psx.getBits(&obj.unk6C[8], "actorId"));

    composed: {
        if (attachment >= 4 and obj.unk12 != 0xFF) {
            const holder = psx.at(v.g.D_800F4538(), obj.unk12).*;
            _ = c.func_800A190C(@as(c_int, obj.unk12), 0xF0, psx.cptr(c.SVECTOR, &anchors[0]), 1);
            _ = c.func_800A190C(@as(c_int, obj.unk12), 0xF1, psx.cptr(c.SVECTOR, &anchors[1]), 1);
            setRotMatrix(cam);
            setTransMatrix(cam);
            scratch.translation.vx = psx.conv(i16, @divTrunc(@as(c_int, anchors[0].vx) +% anchors[1].vx, 2));
            scratch.translation.vy = psx.conv(i16, @divTrunc(@as(c_int, anchors[0].vy) +% anchors[1].vy, 2));
            scratch.translation.vz = psx.conv(i16, @divTrunc(@as(c_int, anchors[0].vz) +% anchors[1].vz, 2));
            ldv0(0x1F8003A8);
            rtv0tr2();
            const pose: *v.SVECTOR = @ptrCast(&obj.unk6C[5]);
            scratch.rotation.vx = psx.conv(i16, @as(c_int, pose.vx) +% obj.unk24);
            scratch.rotation.vy = psx.conv(i16, @as(c_int, pose.vy) +% obj.unk26);
            scratch.rotation.vz = psx.conv(i16, @as(c_int, pose.vz) +% psx.conv(i16, obj.unk28));
            stlvnl(@as(*[3]i32, @ptrFromInt(0x1F8003D4)));
            c.func_800B17F0(psx.cptr(c.SVECTOR, svAt(0x1F8003E0)), psx.cptr(c.MATRIX, matAt(0x1F8003C0)));
            scratch.rotation.vx = holder.?.unk0.pitch;
            scratch.rotation.vy = holder.?.unk0.facing;
            scratch.rotation.vz = holder.?.unk0.roll;
            c.func_800B17F0(psx.cptr(c.SVECTOR, svAt(0x1F8003E0)), psx.cptr(c.MATRIX, &holderRotation));
            c.func_800B196C(psx.cptr(c.MATRIX, &holderRotation), psx.cptr(c.MATRIX, matAt(0x1F8003C0)));
        } else if (attachment < 2) {
            if (obj.unk1A != 0) {
                scratch.rotation.vx = obj.unk24;
                scratch.rotation.vy = obj.unk26;
                scratch.rotation.vz = psx.conv(i16, obj.unk28);
                stlvnl(@as(*[3]i32, @ptrFromInt(0x1F8003D4)));
                _ = c.RotMatrixYXZ_gte(psx.cptr(c.SVECTOR, svAt(0x1F8003E0)), psx.cptr(c.MATRIX, matAt(0x1F8003C0)));
                c.func_800B196C(psx.cptr(c.MATRIX, cam), psx.cptr(c.MATRIX, matAt(0x1F8003C0)));
                // CompMatrixLV called through the two-argument form (m0 = m0 * m1), as the C does.
                const CompMatrixLV2 = *const fn ([*c]c.MATRIX, [*c]c.MATRIX) callconv(.c) void;
                @as(CompMatrixLV2, @ptrCast(&c.CompMatrixLV))(
                    psx.cptr(c.MATRIX, matAt(0x1F8003C0)),
                    psx.cptr(c.MATRIX, @as(*v.MATRIX, @ptrCast(&obj.unk6C[4]))),
                );
                break :composed;
            }
            matAt(0x1F8003C0).* = @as(*v.MATRIX, @ptrCast(&obj.unk6C[4])).*;
            stlvnl(@as(*[3]i32, @ptrFromInt(0x1F8003D4)));
        } else {
            const pose: *v.SVECTOR = @ptrCast(&obj.unk6C[5]);
            scratch.rotation.vx = pose.vx;
            scratch.rotation.vy = pose.vy;
            scratch.rotation.vz = pose.vz;
            stlvnl(&scratch.matrix.t);
            scratch.rotation.vx = psx.conv(i16, @as(c_int, scratch.rotation.vx) +% obj.unk24);
            scratch.rotation.vy = psx.conv(i16, @as(c_int, scratch.rotation.vy) +% obj.unk26);
            scratch.rotation.vz = psx.conv(i16, @as(c_int, scratch.rotation.vz) +% psx.conv(i16, obj.unk28));
            @as(*align(1) c_int, @ptrCast(&scratch.rotation)).* &= 0x0FFF0FFF;
            @as(*align(1) c_int, @ptrCast(&scratch.rotation.vz)).* &= 0xFFF;
            c.func_800B17F0(psx.cptr(c.SVECTOR, &scratch.rotation), psx.cptr(c.MATRIX, &scratch.matrix));
        }
        c.func_800B196C(psx.cptr(c.MATRIX, cam), psx.cptr(c.MATRIX, &scratch.matrix));
    }

    c.func_800B396C(
        psx.cptr(c.MATRIX, &scratch.matrix),
        psx.cptr(c.VECTOR, @as(*v.VECTOR, @ptrCast(&v.g.D_800F3C58()[63]))),
    );
    v.g.D_800F3C58()[0] = scratch.matrix;
    @as(*v.MATRIX, @ptrCast(&obj.unk6C[0])).* = scratch.matrix;
    if (psx.byteAt(c_int, obj, 0x2C).* != 0x400040 or obj.unk30 != 64) {
        scratch.scale.vx = obj.unk2C;
        scratch.scale.vy = obj.unk2E;
        scratch.scale.vz = obj.unk30;
        scratch.scale.vx <<= 6;
        scratch.scale.vy <<= 6;
        scratch.scale.vz <<= 6;
        c.func_800B396C(psx.cptr(c.MATRIX, &scratch.matrix), psx.cptr(c.VECTOR, &scratch.scale));
        v.g.D_800F3C58()[0] = scratch.matrix;
        @as(*v.MATRIX, @ptrCast(&obj.unk6C[0])).* = scratch.matrix;
    }
    v.g.D_800F3C78().* = scratch.matrix;
    @as(*v.MATRIX, @ptrCast(&obj.unk6C[1])).* = scratch.matrix;
    if ((psx.byteAt(c_int, obj, 8).* & 2) != 0) {
        if (c.func_800B3A68(
            obj,
            psx.cptr(c.MATRIX, @as(*v.MATRIX, @ptrCast(&obj.unk6C[0]))),
            (-%v.g.vs_main_nearClip().*) << 2,
            psx.get(obj.unk68),
        ) != 0) {
            scratch.tpage = c.GetTPage(0, 0, 0x40, 0x100);
            if (obj.unk64.unk0 != 0 or ((psx.byteAt(c_uint, obj, 8).* >> 8) & 0xF) >= 6) {
                scratch.clut = c.GetClut((@as(c_int, obj.unkF) << 4) +% 0x300, 0xFF);
            } else {
                scratch.clut = c.GetClut((@as(c_int, obj.unkF) << 4) +% 0x300, 0xED);
            }
            const flags: c_uint = psx.byteAt(c_uint, obj, 8).*;
            var value = false;
            if ((((flags >> 8) & 0xF) -% 4) < 5) {
                if ((flags & 0xF00) != 0x600) value = true;
            }
            if (((flags >> 8) & 0xF) >= 12) value = true;
            if (value) {
                c.func_800B5C30(v.g.D_800F2458(), obj, &obj.unk54, psx.get(scratch.model));
                return;
            }
            c.func_800B4594(v.g.D_800F2458(), obj, &obj.unk54, psx.get(scratch.model));
        }
    }
}
