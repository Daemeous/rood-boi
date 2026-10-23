//! Queues one frame of an actor's texture-map sprite as a DR_MOVE packet
//! (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AB098 (src/BATTLE/BATTLE.PRG/40564.c)
/// Draws animation frame `frame` of `index` from the actor's texture-map overlay as a move packet.
pub fn func_800AB098(actor: ?*v.D_800F4538_t, index: c_int, frame: c_int) callconv(.c) void {
    const a = actor.?;
    const sel: c_int = v.g.D_800E8F2C().*;
    if (psx.at(&a.unk183E, sel).* == 0xFF) return;

    // &actor->unk1802[2] + index: byte offset 0x1804, then 4-byte steps.
    const anim_off: c_int = 0x1804 +% index *% 4;
    var anim: *v.func_800AB098_t = psx.byteAt(v.func_800AB098_t, a, @as(isize, anim_off));
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

    if ((psx.byteAt(c_int, a, 0x5AC).* & 0x0800_0000) == 0) {
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

    // vs_battlePacketBegin: the packet cursor is a 4-byte guest address in the scratchpad.
    const cursor: u32 = @as(*const u32, @ptrFromInt(0x1F800000)).*;
    const packet: *v.DR_MOVE = @ptrFromInt(cursor);

    const slot: c_int = a.unk5BB;
    const x: c_int = psx.byteAt(i16, v.g.D_800E8F30(), slot * 4).*;
    const y: c_int = psx.byteAt(i16, v.g.D_800E8F30(), slot * 4 + 2).*;
    const row: c_int = psx.at(v.g.D_800E8F90(), slot).*;
    const py: c_int = overlay.y;
    const f: c_int = @as(c_int, psx.at(v.g.D_800F2450(), sel).*) & 15;

    var rect: v.RECT = undefined;
    rect.x = psx.conv(i16, (f << 6) +% srcX);
    rect.y = psx.conv(i16, srcY +% 256);
    rect.w = psx.conv(i16, width);
    rect.h = psx.conv(i16, height);

    srcY = (y << 8) +% (row << 6) +% py;
    c.SetDrawMove(psx.cptr(c.DR_MOVE, packet), psx.cptr(c.RECT, &rect), dstX +% (x << 6), srcY);
    c.func_8007A824(psx.cptr(c.DR_MOVE, packet));

    // vs_battlePacketEnd: the cursor advances by one DR_MOVE.
    @as(*u32, @ptrFromInt(0x1F800000)).* = psx.addr(packet) +% @sizeOf(v.DR_MOVE);
}
