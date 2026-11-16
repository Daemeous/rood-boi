//! The room submission of pc/src/scene_submit.inc (compiled into pc/src/game/BATTLE/BATTLE.PRG/146C.c): the
//! scene's packets, the area map's frame and its host queries.
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");
const sprite_item = @import("common").sprite_item;

/// The scene's view.
fn view() *v.vs_view_t {
    return v.g.vs_view();
}

fn scratch() *v.vs_scratch_t {
    return v.g.vs_scratch();
}

const Anchor = *const fn (?*const anyopaque, ?*const anyopaque, c_int) callconv(.c) void;

/// orig: anchor_map (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Anchors the area map's packets (from `start` to the packet cursor) to the right edge of the screen.
pub fn anchorMap(start: ?*anyopaque) callconv(.c) void {
    if (psx.weak(Anchor, "pc_hw_anchor_packets")) |hook| hook(start, psx.get(scratch().unk0), 2);
}

/// `(u_long*)ot + 5`: the ordering table entry five words in.
fn otPlus5() ?*anyopaque {
    const base: usize = @intFromPtr(psx.get(scratch().unk4));
    return @ptrFromInt(base +% 20);
}

/// orig: vs_field_drawRoomScene (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Submits the room: the effects and actors, the room's geometry with the facing bits of the compass, and the area map with its frame.
pub fn vs_field_drawRoomScene() callconv(.c) void {
    var savedOffset: v.vs_battle_geomOffset = undefined;
    var mapOffset: v.vs_battle_geomOffset = undefined;
    var clip: v.RECT = undefined;
    var rotation: v.MATRIX = undefined;

    @import("common").gte.setFrame(&view().viewMatrix);
    c.vs_field_drawRoomTriggerMarkers();
    c.func_8009627C();
    c.vs_field_updateGimmickAnimationQueue();
    if (v.g.vs_field_areRoomUpdatesPaused().* == 0) {
        var i: c_int = 0;
        while (i < v.g.vs_gametime_tickspeed().* >> 1) : (i +%= 1) {
            c.vs_field_updateRoomEffects();
        }
    }
    c.vs_field_drawMovingPlatformShadows();
    c.vs_field_updateRoomWeather();

    const state: u16 = roomState();
    if (state != 0x101) return;
    if (psx.get(v.g.vs_battle_roomData().geometrySection) != null) {
        const vy = view().camera.angles.vy;
        // the masked angle is 0..0xFFF, so the shift amounts are 0..7
        const left: c_int = @as(c_int, 1) << @intCast((((0x9C0 -% vy) & 0xFFF) >> 9));
        const right: c_int = @as(c_int, 1) << @intCast((((0x840 -% vy) & 0xFFF) >> 9));
        const front: c_int = @as(c_int, 1) << @intCast((((0x900 -% vy) & 0xFFF) >> 9));
        view().firstPersonViewFacing = (view().firstPersonViewFacing & 0xFFFF_FF00) |
            psx.conv(c_uint, left) | psx.conv(c_uint, right) | psx.conv(c_uint, front);
        c.vs_field_drawRoomGeometry(psx.get(v.g.vs_battle_roomData().geometrySection));
    }
    if (roomState() != state or (v.g.vs_main_settings().simpleMap & 1) == 0 or
        psx.get(v.g.vs_battle_roomData().sectionF) == null)
    {
        return;
    }

    if (v.g.vs_field_isMinimapVisible().* != 0) {
        v.g.vs_field_minimapSlideFrame().* +%= 1;
        if (v.g.vs_field_minimapSlideFrame().* > 8) v.g.vs_field_minimapSlideFrame().* = 8;
    } else {
        v.g.vs_field_minimapSlideFrame().* -%= 2;
        if (v.g.vs_field_minimapSlideFrame().* < 0) v.g.vs_field_minimapSlideFrame().* = 0;
    }
    if (v.g.vs_field_minimapSlideFrame().* <= 0) return;

    c.vs_field_buildMinimapRotation(psx.cptr(c.MATRIX, &rotation), 0x800 -% view().camera.angles.vy);
    // The map's drawing areas and frame are draw items placed at the table's slot 5 (the packets AddPrim linked there): the
    // first area is the other buffer's whole one, the second the map's window.
    const mapPackets: ?*anyopaque = psx.get(scratch().unk0);
    const other = &v.g.vs_main_drawEnv()[@intCast((v.g.vs_main_frameBuf().* +% 1) & 1)].clip;
    sprite_item.area(otPlus5(), other.x, other.y, other.w, other.h, null);

    if (other.x < 320) {
        clip.x = 216;
    } else {
        clip.x = 536;
    }
    clip.y = 156;
    clip.w = 96;
    clip.h = 64;
    mapOffset.y = 196;
    mapOffset.x = 392 -% (@as(c_int, v.g.vs_field_minimapSlideFrame().*) << 4);
    c.vs_battle_getGeomOffset(psx.cptr(c.vs_battle_geomOffset, &savedOffset));
    c.vs_battle_setGeomOffset(psx.cptr(c.vs_battle_geomOffset, &mapOffset));
    v.g.pc_gte_anchored().* = 1;
    const mapView = @import("common").gte.viewOf(&rotation, @import("common").gte.screenCurrent());
    c.vs_field_drawMinimapScene(psx.cptr(c.vs_battle_scene, psx.get(v.g.vs_battle_roomData().sectionF).?), @ptrCast(&mapView));
    v.g.pc_gte_anchored().* = 0;
    c.vs_battle_setGeomOffset(psx.cptr(c.vs_battle_geomOffset, &savedOffset));

    sprite_item.area(otPlus5(), clip.x, clip.y, clip.w, clip.h, null);

    if ((v.g.vs_main_settings().simpleMap & 2) == 0) {
        anchorMap(mapPackets);
        return;
    }

    // The frame's colours (the corners of its outline and the ends of its left edge): pulsing red while the weapon is drawn, else green.
    var frame_rgb: [4]c_int = undefined;
    var edge_rgb: [2]c_int = undefined;
    if (psx.get(v.g.vs_battle_characterState().*).?.weaponDrawn == 1) {
        const pulse = @divTrunc(c.rsin(v.g.vs_field_minimapBorderPulsePhase().* << 8), 64);
        v.g.vs_field_minimapBorderPulsePhase().* +%= @divTrunc(v.g.vs_gametime_tickspeed().*, 2);
        const red: c_int = 128 -% pulse;
        frame_rgb = .{ rgb(red, 0, 0), rgb(red +% 32, 0, 0), rgb(red +% 63, 0, 0), rgb(red +% 32, 0, 0) };
        edge_rgb = .{ rgb(red +% 32, 0, 0), rgb(red, 0, 0) };
    } else {
        v.g.vs_field_minimapBorderPulsePhase().* = 0;
        frame_rgb = .{ rgb(0, 128, 0), rgb(0, 192, 0), rgb(0, 255, 0), rgb(0, 192, 0) };
        edge_rgb = .{ rgb(0, 192, 0), rgb(0, 128, 0) };
    }
    // Added last drawn first: the outline's three segments (the polyline's, in order), the left edge, then the draw mode (B+F).
    const corners = [4]c_int{ xyOf(215, 155), xyOf(313, 155), xyOf(313, 220), xyOf(215, 220) };
    var seg: usize = 3;
    while (seg > 0) {
        seg -= 1;
        _ = sprite_item.lineG2(otPlus5(), true, frame_rgb[seg], corners[seg], frame_rgb[seg + 1], corners[seg + 1]);
    }
    _ = sprite_item.lineG2(otPlus5(), true, edge_rgb[0], xyOf(215, 220), edge_rgb[1], xyOf(215, 155));
    c.pc_insert_tpage(5, 96);
    anchorMap(mapPackets);
}

/// A colour word from its parts (the low byte of each).
fn rgb(r: c_int, g: c_int, b: c_int) c_int {
    return (r & 0xFF) | (g & 0xFF) << 8 | (b & 0xFF) << 16;
}

/// A corner's packed screen position (x low, y high).
fn xyOf(x: c_int, y: c_int) c_int {
    return (x & 0xFFFF) | y << 16;
}

/// `*(u_short*)&vs_battle_roomData`: the room section's first two bytes.
fn roomState() u16 {
    const p: *const u16 = @ptrCast(v.g.vs_battle_roomData());
    return p.*;
}

/// orig: pc_battle_action (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Host query for the interactive fog: the battle action running now (performer, action, phase, chain step, kind, aim point and first target actor); 0 when none runs.
pub fn battleAction(actor: *c_int, action: *c_int, phase: *c_int, chain: *c_int, kind: *c_int, plain: *c_int, aim: *[3]c_int, target: *c_int) callconv(.c) c_int {
    if (v.g.D_800F196C().* != 2 or v.g.vs_battle_actionState().* == null) return 0;
    const block = v.g.vs_battle_actionState().*.?;
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

/// orig: pc_battle_event_running (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Whether an event loads or runs.
pub fn battleEventRunning() callconv(.c) c_int {
    return @intFromBool(v.g.vs_event_playbackPhase().* != 0);
}

/// orig: pc_battle_event_id (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// The running event's id ((source << 12) | event file), or -1 when none runs.
pub fn battleEventId() callconv(.c) c_int {
    if (v.g.vs_event_playbackPhase().* == 0) return -1;
    const id: c_int = (@as(c_int, v.g.vs_event_eventSource().*) << 12) | @as(c_int, v.g._evtFile().*);
    return @as(c_int, psx.conv(u16, id));
}

/// orig: pc_battle_camera_target (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// The point the battle camera follows in its modes 1 and 3 (weapon drawn, a target chosen); 0 when there is none.
pub fn battleCameraTarget(position: *[3]c_int) callconv(.c) c_int {
    const character = psx.get(v.g.vs_battle_characterState().*);
    if (character == null or character.?.weaponDrawn != 1 or v.g.vs_battle_actionState().* == null) return 0;
    const block = v.g.vs_battle_actionState().*.?;
    if (block.unk298D == 0xFF or block.unk2C00 == 0 or block.unk298D >= 23) return 0;
    position[0] = block.unk29C0[block.unk298D].unk0.vx;
    position[1] = block.unk29C0[block.unk298D].unk0.vy;
    position[2] = block.unk29C0[block.unk298D].unk0.vz;
    return 1;
}

/// The script pointer of event thread `i` (vs_event_threadCursors holds guest-range byte pointers).
fn eventThread(i: usize) ?[*]u8 {
    return @ptrCast(v.g.vs_event_threadCursors()[i]);
}

/// orig: pc_battle_camera_focus (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Host query for the depth of field's camera subject. out: [0] the battle state D_800F196C (-1 without the action block), [1] the camera mode,
/// [2] a target candidate is chosen, [3..5] its point, [6] an area point, [7..9] it, [10..12] the camera's look-at in world units, [13] the candidate's actor slot.
pub fn battleCameraFocus(out: *[14]c_int) callconv(.c) void {
    @memset(out, 0);
    const block = v.g.vs_battle_actionState().*;
    out[0] = if (block != null) psx.conv(c_int, v.g.D_800F196C().*) else -1;
    out[1] = psx.conv(c_int, v.g._cameraMode().*);
    if (block != null and v.g.D_800F196C().* == 2) {
        const b = block.?;
        const mode = v.g._cameraMode().*;
        if ((mode == 1 or mode == 3) and b.unk298D != 0xFF and b.unk2C00 != 0 and b.unk298D < 23) {
            out[2] = 1;
            out[3] = b.unk29C0[b.unk298D].unk0.vx;
            out[4] = b.unk29C0[b.unk298D].unk0.vy;
            out[5] = b.unk29C0[b.unk298D].unk0.vz;
            out[13] = b.unk29C0[b.unk298D].unk9;
        } else if (mode == 2) {
            out[6] = 1;
            out[7] = b.unk2998.vx;
            out[8] = b.unk2998.vy;
            out[9] = b.unk2998.vz;
        }
    }
    out[10] = v.g.vs_view().camera.lookAt.vx >> 12;
    out[11] = v.g.vs_view().camera.lookAt.vy >> 12;
    out[12] = v.g.vs_view().camera.lookAt.vz >> 12;
}

/// orig: pc_battle_event_threads (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Host query for the cutscene player: the event runner's state and skip state, and per script thread its offset in the script buffer (-1 when not running), the opcode there and that opcode's handler; returns whether an event loads or runs.
pub fn battleEventThreads(state: *[2]c_int, offset: *[4]c_int, opcode: *[4]c_int, handler: *[4]?*anyopaque) callconv(.c) c_int {
    const running = v.g.vs_event_playbackPhase().*;
    state[0] = running;
    state[1] = v.g.vs_event_skipState().*;
    for (0..4) |i| {
        const pc = if (running == 4) eventThread(i) else null;
        const base = v.g.vs_event_scriptBuffer().*;
        offset[i] = if (pc != null and base != null) psx.conv(c_int, @intFromPtr(pc.?) -% @intFromPtr(base.?)) else -1;
        opcode[i] = if (pc != null) pc.?[0] else -1;
        handler[i] = if (pc != null) @ptrCast(@constCast(v.g._opcodeFunctionTable()[pc.?[0]])) else null;
    }
    return @intFromBool(running != 0);
}

/// orig: pc_battle_event_arguments (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Host query for the cutscene sequencer: the raw operand bytes of the opcode thread `thread` is at (the count is the interpreter's operand size less the opcode), the text box line of opcode 0x11 and, for the object handlers, the actor its first operand names; 0 when the thread is not running.
pub fn battleEventArguments(thread: c_int, args: [*]c_int, line: *c_int, actor: *c_int) callconv(.c) c_int {
    line.* = -1;
    actor.* = -1;
    if (v.g.vs_event_playbackPhase().* != 4 or thread < 0 or thread >= 4) return 0;
    const pc = eventThread(@intCast(thread)) orelse return 0;
    const n: c_int = @as(c_int, v.g.vs_event_opcodeSizes()[pc[0]]) - 1;
    if (n < 0 or n > 16) return 0;
    if (pc[0] == 0x11 and n >= 3) line.* = pc[2];
    switch (pc[0]) {
        // object placement, move, face, transform and animation: the first short is an actor reference
        0x20, 0x21, 0x22, 0x2b, 0x2c, 0x33, 0x34, 0x35, 0x36 => {
            if (n >= 2) {
                const ref: c_uint = @as(c_uint, pc[1]) | (@as(c_uint, pc[2]) << 8);
                if ((ref >> 12) < 2) {
                    const resolved = c.vs_event_resolveScriptActorReference(psx.conv(u16, ref));
                    if (resolved >= 0 and resolved <= 16) actor.* = resolved;
                }
            }
        },
        else => {},
    }
    var i: usize = 0;
    while (i < n) : (i += 1) args[i] = pc[i + 1];
    return n;
}

/// orig: pc_battle_event_waiting (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Host query for the cutscene viewer's auto-advance: the pad bits the running event's threads wait on (opcode 0x15's mask; Circle for a shown text box or a screen of its own), 0 when nothing.
pub fn battleEventWaiting() callconv(.c) c_int {
    if (v.g.vs_event_playbackPhase().* != 4) return 0;
    var mask: c_int = 0;
    for (0..4) |i| {
        const pc = eventThread(i) orelse continue;
        if (pc[0] == 0x15) {
            mask |= pc[1];
        } else if (pc[0] == 0x11 and pc[1] < 8 and v.g.vs_battle_textBoxStatuses()[pc[1]] != 0) {
            mask |= 0x20;
        } else if (pc[0] == 0x59 and v.g.vs_event_isMenuTransitionActive().* != 0) {
            mask |= 0x20;
        }
    }
    return mask;
}
