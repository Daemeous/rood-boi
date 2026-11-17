//! Queues one frame of an actor's texture-map sprite as a DR_MOVE packet
//! (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_textureFrameMove (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Draws animation frame `frame` of `index` from the actor's texture-map overlay as a move packet.
pub fn vs_actors_textureFrameMove(actor: ?*v.D_800F4538_t, index: c_int, frame: c_int) callconv(.c) void {
    const a = actor.?;
    const sel: c_int = v.g.vs_actors_sharedSequenceBankIndex().*;
    if (psx.at(&a.unk183E, sel).* == 0xFF) return;

    // &actor->unk1802[2] + index: 4-byte steps from the third byte of unk1802.
    const anim_off: c_int = 2 +% index *% 4;
    var anim: *v.func_800AB098_t = psx.fieldAt(v.func_800AB098_t, &a.unk1802, @as(isize, anim_off));
    var fallback: v.func_800AB098_t = undefined;
    if (anim.columns == 0) {
        fallback.overlay = if (index != 0) 7 else 5;
        const offset: c_int = index *% 4;
        fallback.x = psx.at(&a.unk614, offset).*;
        fallback.y = psx.at(&a.unk614, offset +% 1).*;
        fallback.columns = psx.at(&a.unk614, offset +% 2).*;
        if (fallback.columns == 0) return;
        anim = &fallback;
    }

    var srcX: c_int = anim.x;
    var srcY: c_int = anim.y;
    // anim->overlay - 1 with overlay 0 gives -1: the C indexes before the array.
    const overlay: *v.texMapOverlay_t = psx.at(&a.texMapOverlays, @as(c_int, anim.overlay) - 1);
    var dstX: c_int = overlay.x;
    var width: c_int = overlay.w;
    const height: c_int = overlay.h;

    if ((psx.bitsAt(c_int, &a._bits_unk5AC_0, 0).* & 0x0800_0000) == 0) {
        srcX = @divTrunc(srcX, 4);
        width = @divTrunc(width, 4);
        dstX = @divTrunc(dstX, 4);
    } else {
        srcX = @divTrunc(srcX, 2);
        width = @divTrunc(width, 2);
        dstX = @divTrunc(dstX, 2);
    }

    // columns is nonzero here (checked above), so the remainder and quotient are defined.
    const cols: c_int = anim.columns;
    srcX +%= @rem(frame, cols) *% width;
    srcY +%= @divTrunc(frame, cols) *% height;
    srcY +%= @as(c_int, psx.at(&a.unk183E, sel).*);

    // vs_battlePacketBegin
    const packet: *v.DR_MOVE = @ptrCast(@alignCast(common.scratch.get().unk0));

    const slot: c_int = a.unk5BB;
    const x: c_int = psx.byteAt(i16, v.g.vs_actors_textureSlotOrigins(), slot * 4).*;
    const y: c_int = psx.byteAt(i16, v.g.vs_actors_textureSlotOrigins(), slot * 4 + 2).*;
    const row: c_int = psx.at(v.g.vs_actors_textureSlotRowOffsets(), slot).*;
    const py: c_int = overlay.y;
    const f: c_int = @as(c_int, psx.at(v.g.D_800F2450(), sel).*) & 15;

    var rect: v.RECT = undefined;
    rect.x = psx.conv(i16, (f << 6) +% srcX);
    rect.y = psx.conv(i16, srcY +% 256);
    rect.w = psx.conv(i16, width);
    rect.h = psx.conv(i16, height);

    srcY = (y << 8) +% (row << 6) +% py;
    c.pc_frame_move(@ptrCast(packet), @ptrCast(&rect), dstX +% (x << 6), srcY);

    // vs_battlePacketEnd: the cursor advances by one DR_MOVE.
    common.scratch.get().unk0 = psx.at(packet, 1);
}
