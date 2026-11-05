//! The room submission of pc/src/scene_submit.inc (compiled into src/BATTLE/BATTLE.PRG/146C.c): the
//! scene's packets, the area map's frame and its host queries.
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// The scratchpad block, addressed by number as the C does.
fn scratch() *v.vs_scratch_t {
    return @ptrFromInt(0x1F800000);
}

const Anchor = *const fn (?*const anyopaque, ?*const anyopaque, c_int) callconv(.c) void;

/// orig: anchor_map (src/BATTLE/BATTLE.PRG/146C.c)
/// Anchors the area map's packets (from `start` to the packet cursor) to the right edge of the screen.
pub fn anchorMap(start: ?*anyopaque) callconv(.c) void {
    if (psx.weak(Anchor, "pc_hw_anchor_packets")) |hook| hook(start, psx.get(scratch().unk0), 2);
}

/// `(u_long*)ot + 5`: the ordering table entry five words in.
fn otPlus5() ?*anyopaque {
    const base: usize = @intFromPtr(psx.get(scratch().unk4));
    return @ptrFromInt(base +% 20);
}

/// setLineG4 / setLineG2: the primitive's length and code.
fn setTag(prim: anytype, len: u8, code: u8) void {
    const tag: *v.P_TAG = @ptrCast(prim);
    psx.setBits(tag, "len", len);
    tag.code = code;
}

/// setSemiTrans(prim, 1).
fn setSemiTrans(prim: anytype) void {
    const tag: *v.P_TAG = @ptrCast(prim);
    tag.code = tag.code | 0x02;
}

/// orig: func_8008AC78 (src/BATTLE/BATTLE.PRG/146C.c)
/// Submits the room: the effects and actors, the room's geometry with the facing bits of the compass, and the area map with its frame.
pub fn func_8008AC78() callconv(.c) void {
    var savedOffset: v.vs_battle_geomOffset = undefined;
    var mapOffset: v.vs_battle_geomOffset = undefined;
    var clip: v.RECT = undefined;
    var rotation: v.MATRIX = undefined;

    c.SetRotMatrix(psx.cptr(c.MATRIX, &scratch().viewMatrix));
    c.SetTransMatrix(psx.cptr(c.MATRIX, &scratch().viewMatrix));
    c.func_8008DF14();
    c.func_8009627C();
    c.func_8008C0C0();
    if (v.g.D_800F1BA4().* == 0) {
        var i: c_int = 0;
        while (i < v.g.vs_gametime_tickspeed().* >> 1) : (i +%= 1) {
            c.func_80091B04();
        }
    }
    c.func_8008CEB0();
    c.func_80090EEC();

    const state: u16 = roomState();
    if (state != 0x101) return;
    if (psx.get(v.g.vs_battle_roomData().geometrySection) != null) {
        const vy = scratch().camera.angles.vy;
        // the masked angle is 0..0xFFF, so the shift amounts are 0..7
        const left: c_int = @as(c_int, 1) << @intCast((((0x9C0 -% vy) & 0xFFF) >> 9));
        const right: c_int = @as(c_int, 1) << @intCast((((0x840 -% vy) & 0xFFF) >> 9));
        const front: c_int = @as(c_int, 1) << @intCast((((0x900 -% vy) & 0xFFF) >> 9));
        scratch().firstPersonViewFacing = (scratch().firstPersonViewFacing & 0xFFFF_FF00) |
            psx.conv(c_uint, left) | psx.conv(c_uint, right) | psx.conv(c_uint, front);
        c.func_8008B1FC(psx.get(v.g.vs_battle_roomData().geometrySection));
    }
    if (roomState() != state or (v.g.vs_main_settings().simpleMap & 1) == 0 or
        psx.get(v.g.vs_battle_roomData().sectionF) == null)
    {
        return;
    }

    if (v.g.D_800F1CD6().* != 0) {
        v.g.D_800F1CD4().* +%= 1;
        if (v.g.D_800F1CD4().* > 8) v.g.D_800F1CD4().* = 8;
    } else {
        v.g.D_800F1CD4().* -%= 2;
        if (v.g.D_800F1CD4().* < 0) v.g.D_800F1CD4().* = 0;
    }
    if (v.g.D_800F1CD4().* <= 0) return;

    c.func_8008B590(psx.cptr(c.MATRIX, &rotation), 0x800 -% scratch().camera.angles.vy);
    c.SetRotMatrix(psx.cptr(c.MATRIX, &rotation));
    var area: *v.DR_AREA = @ptrCast(@alignCast(psx.get(scratch().unk0).?));
    const mapPackets: *v.DR_AREA = area;
    c.SetDrawArea(psx.cptr(c.DR_AREA, area), psx.cptr(c.RECT, &v.g.vs_main_drawEnv()[@intCast((v.g.vs_main_frameBuf().* +% 1) & 1)].clip));
    c.AddPrim(otPlus5(), area);
    area = psx.at(area, 1);
    psx.put(&scratch().unk0, area);

    if (v.g.vs_main_drawEnv()[@intCast((v.g.vs_main_frameBuf().* +% 1) & 1)].clip.x < 320) {
        clip.x = 216;
    } else {
        clip.x = 536;
    }
    clip.y = 156;
    clip.w = 96;
    clip.h = 64;
    mapOffset.y = 196;
    mapOffset.x = 392 -% (@as(c_int, v.g.D_800F1CD4().*) << 4);
    c.vs_battle_getGeomOffset(psx.cptr(c.vs_battle_geomOffset, &savedOffset));
    c.vs_battle_setGeomOffset(psx.cptr(c.vs_battle_geomOffset, &mapOffset));
    v.g.pc_gte_anchored().* = 1;
    c.func_80098F8C(psx.cptr(c.vs_battle_scene, psx.get(v.g.vs_battle_roomData().sectionF).?));
    v.g.pc_gte_anchored().* = 0;
    c.vs_battle_setGeomOffset(psx.cptr(c.vs_battle_geomOffset, &savedOffset));
    c.SetRotMatrix(psx.cptr(c.MATRIX, &scratch().viewMatrix));

    area = @ptrCast(@alignCast(psx.get(scratch().unk0).?));
    c.SetDrawArea(psx.cptr(c.DR_AREA, area), psx.cptr(c.RECT, &clip));
    c.AddPrim(otPlus5(), area);
    area = psx.at(area, 1);
    psx.put(&scratch().unk0, area);

    if ((v.g.vs_main_settings().simpleMap & 2) == 0) {
        c.anchor_map(mapPackets);
        return;
    }

    const frame: *v.LINE_G4 = @ptrCast(@alignCast(area));
    setTag(frame, 9, 0x5C);
    frame.pad = 0x55555555;
    frame.p2 = 0;
    frame.p3 = 0;
    frame.x0 = 215;
    frame.y0 = 155;
    frame.x1 = 313;
    frame.y1 = 155;
    frame.x2 = 313;
    frame.y2 = 220;
    frame.x3 = 215;
    frame.y3 = 220;
    setSemiTrans(frame);
    c.AddPrim(otPlus5(), frame);
    var line: *v.LINE_G2 = @ptrCast(@alignCast(psx.at(frame, 1)));
    setTag(line, 4, 0x50);
    setSemiTrans(line);
    line.y0 = 220;
    line.x0 = 215;
    line.x1 = 215;
    line.y1 = 155;
    const edge: *v.LINE_G2 = line;
    line = psx.at(line, 1);
    c.AddPrim(otPlus5(), edge);
    psx.put(&scratch().unk0, line);
    c.pc_insert_tpage(5, 96);
    c.anchor_map(mapPackets);

    if (psx.get(v.g.vs_battle_characterState().*).?.weaponDrawn == 1) {
        const pulse = @divTrunc(c.rsin(v.g.D_800F1BA0().* << 8), 64);
        v.g.D_800F1BA0().* +%= @divTrunc(v.g.vs_gametime_tickspeed().*, 2);
        const red: c_int = 128 -% pulse;
        setRgb(frame, 0, red, 0, 0);
        setRgb(frame, 1, red +% 32, 0, 0);
        setRgb(frame, 2, red +% 63, 0, 0);
        setRgb(frame, 3, red +% 32, 0, 0);
        edge.r0 = psx.conv(u8, red +% 32);
        edge.g0 = 0;
        edge.b0 = 0;
        edge.r1 = psx.conv(u8, red);
        edge.g1 = 0;
        edge.b1 = 0;
    } else {
        v.g.D_800F1BA0().* = 0;
        setRgb(frame, 0, 0, 128, 0);
        setRgb(frame, 1, 0, 192, 0);
        setRgb(frame, 2, 0, 255, 0);
        setRgb(frame, 3, 0, 192, 0);
        edge.r0 = 0;
        edge.g0 = 192;
        edge.b0 = 0;
        edge.r1 = 0;
        edge.g1 = 128;
        edge.b1 = 0;
    }
}

/// `*(u_short*)&vs_battle_roomData`: the room section's first two bytes.
fn roomState() u16 {
    const p: *const u16 = @ptrCast(v.g.vs_battle_roomData());
    return p.*;
}

/// setRGB0..3 of a LINE_G4.
fn setRgb(prim: *v.LINE_G4, comptime corner: u2, r: c_int, g: c_int, b: c_int) void {
    switch (corner) {
        0 => {
            prim.r0 = psx.conv(u8, r);
            prim.g0 = psx.conv(u8, g);
            prim.b0 = psx.conv(u8, b);
        },
        1 => {
            prim.r1 = psx.conv(u8, r);
            prim.g1 = psx.conv(u8, g);
            prim.b1 = psx.conv(u8, b);
        },
        2 => {
            prim.r2 = psx.conv(u8, r);
            prim.g2 = psx.conv(u8, g);
            prim.b2 = psx.conv(u8, b);
        },
        3 => {
            prim.r3 = psx.conv(u8, r);
            prim.g3 = psx.conv(u8, g);
            prim.b3 = psx.conv(u8, b);
        },
    }
}

/// orig: pc_battle_action (src/BATTLE/BATTLE.PRG/146C.c)
/// Host query for the interactive fog: the battle action running now (performer, action, phase, chain step, kind, aim point and first target actor); 0 when none runs.
pub fn battleAction(actor: *c_int, action: *c_int, phase: *c_int, chain: *c_int, kind: *c_int, plain: *c_int, aim: *[3]c_int, target: *c_int) callconv(.c) c_int {
    if (v.g.D_800F196C().* != 2 or v.g.D_800F19CC().* == null) return 0;
    const block = v.g.D_800F19CC().*.?;
    const a = &block.unk854[block.unk0 & 3];
    actor.* = a.unk4.unk0.targetActor;
    action.* = a.actionIndex;
    phase.* = psx.conv(c_int, v.g._cameraMode().*);
    chain.* = psx.conv(c_int, block.unk0);
    // the actions table has 256 entries: the index is checked first
    kind.* = if (a.actionIndex < 256) @as(c_int, psx.getBits(&v.g.actions()[a.actionIndex], "type")) else 0;
    plain.* = if (a.actionIndex < 256) @intFromBool(psx.getBits(&v.g.actions()[a.actionIndex], "unk2_0") == 0) else 1;
    aim[0] = block.unk8.unk844.vx;
    aim[1] = block.unk8.unk844.vy;
    aim[2] = block.unk8.unk844.vz;
    target.* = if (a.unk4A != 0 and a.unk4C[0].unk40 == 0) @as(c_int, a.unk4C[0].unk0.targetActor) else -1;
    return 1;
}

/// orig: pc_battle_event_running (src/BATTLE/BATTLE.PRG/146C.c)
/// Whether an event loads or runs.
pub fn battleEventRunning() callconv(.c) c_int {
    return @intFromBool(v.g.D_800F4C34().* != 0);
}

/// orig: pc_battle_event_id (src/BATTLE/BATTLE.PRG/146C.c)
/// The running event's id ((source << 12) | event file), or -1 when none runs.
pub fn battleEventId() callconv(.c) c_int {
    if (v.g.D_800F4C34().* == 0) return -1;
    const id: c_int = (@as(c_int, v.g.D_800F4BE0().*) << 12) | @as(c_int, v.g._evtFile().*);
    return @as(c_int, psx.conv(u16, id));
}

/// orig: pc_battle_camera_target (src/BATTLE/BATTLE.PRG/146C.c)
/// The point the battle camera follows in its modes 1 and 3 (weapon drawn, a target chosen); 0 when there is none.
pub fn battleCameraTarget(position: *[3]c_int) callconv(.c) c_int {
    const character = psx.get(v.g.vs_battle_characterState().*);
    if (character == null or character.?.weaponDrawn != 1 or v.g.D_800F19CC().* == null) return 0;
    const block = v.g.D_800F19CC().*.?;
    if (block.unk298D == 0xFF or block.unk2C00 == 0 or block.unk298D >= 23) return 0;
    position[0] = block.unk29C0[block.unk298D].unk0.vx;
    position[1] = block.unk29C0[block.unk298D].unk0.vy;
    position[2] = block.unk29C0[block.unk298D].unk0.vz;
    return 1;
}
