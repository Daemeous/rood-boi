//! Queues a move packet that copies a texture block of one part of a battle actor to the screen (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// Reads the packet cursor from the cursor (vs_battlePacketBegin).
fn packetBegin() *v.DR_MOVE {
    return @ptrCast(@alignCast(common.scratch.get().unk0));
}

/// Stores the packet cursor past `p` into the cursor (vs_battlePacketEnd).
fn packetEnd(p: *v.DR_MOVE) void {
    common.scratch.get().unk0 = psx.at(p, 1);
}

/// orig: vs_actors_textureOverlayMove (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Queues a move packet copying part `arg[1]` of actor `index` from texture page `arg[2..3]` to the screen.
pub fn vs_actors_textureOverlayMove(index: c_int, arg: [*c]u8) callconv(.c) void {
    // The actor pointer is only dereferenced inside the branch, as in the C.
    const actorPtr: ?*v.D_800F4538_t = psx.at(v.g.vs_actors_actorRecords(), index).*;

    if (v.g.D_800F2450()[v.g.vs_actors_sharedSequenceBankIndex().*] != 0) {
        const actorStruct = actorPtr.?;
        // The part records (texMapOverlays) as bytes.
        const actor: [*]u8 = @ptrCast(&actorStruct.texMapOverlays);

        var srcX: c_int = arg[2];
        var srcY: c_int = arg[3];
        srcY +%= @as(c_int, psx.fieldAt(u8, &actorStruct.unk183C, @as(isize, v.g.vs_actors_sharedSequenceBankIndex().*) + 2).*);

        const partOff: usize = @as(usize, arg[1]) * 4;
        var dstX: c_int = actor[partOff];
        var width: c_int = actor[partOff + 2];
        const height: c_int = actor[partOff + 3];

        if (psx.getBits(actorStruct, "unk5AC_27") == 0) {
            srcX = @divTrunc(srcX, 4);
            width = @divTrunc(width, 4);
            dstX = @divTrunc(dstX, 4);
        } else {
            srcX = @divTrunc(srcX, 2);
            width = @divTrunc(width, 2);
            dstX = @divTrunc(dstX, 2);
        }

        const packet = packetBegin();
        const slot: usize = actorStruct.unk5BB;

        const table = v.g.vs_actors_textureSlotOrigins();
        const x: c_int = @as(*align(1) const i16, @ptrCast(table + slot * 4)).*;
        const y: c_int = @as(*align(1) const i16, @ptrCast(table + slot * 4 + 2)).*;
        const row: c_int = v.g.vs_actors_textureSlotRowOffsets()[slot];
        const py: c_int = actor[partOff + 1];
        const page: c_int = v.g.D_800F2450()[v.g.vs_actors_sharedSequenceBankIndex().*];

        var rect: v.RECT = undefined;
        rect.y = psx.conv(i16, srcY +% 256);
        rect.w = psx.conv(i16, width);
        rect.h = psx.conv(i16, height);
        rect.x = psx.conv(i16, ((page & 15) << 6) +% srcX);

        c.SetDrawMove(
            psx.cptr(c.DR_MOVE, packet),
            psx.cptr(c.RECT, &rect),
            dstX +% (x << 6),
            (y << 8) +% (row << 6) +% py,
        );
        c.vs_render_appendOverlayPrimitive(psx.cptr(c.DR_MOVE, packet));
        packetEnd(packet);
    }
}
