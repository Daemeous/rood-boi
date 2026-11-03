//! The frame's actor, weapon and object transforms: animation, colour fades,
//! texture loads, bones and attachments for every battle slot, then the
//! weapons on their bones and the room objects (src/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

const Actor = v.D_800F4538_t;

/// The draw depth word of the scratchpad frame block (frameScratch.depth at 0x1F800004).
fn depth() *c_int {
    return @ptrFromInt(0x1F80_0004);
}

/// The object offset of the scratchpad transform block (actorTransformScratch.offset at 0x1F80035C).
fn objectOffset() *v.SVECTOR {
    return @ptrFromInt(0x1F80_035C + @offsetOf(v.actorTransformScratch, "offset"));
}

/// The 32-bit flag word at byte `off` of an actor or object.
fn word(p: anytype, off: isize) *align(1) c_uint {
    return psx.byteAt(c_uint, p, off);
}

/// unk17E4's colour (unk1, unk2) stepped towards unk17E8's over unk3 frames.
fn fadeColour(a: *Actor) void {
    const fade = &a.unk17E4;
    if (fade.unk0 == 1 and fade.unk3 != 0) {
        var step: c_int = @divTrunc(v.g.vs_gametime_tickspeed().*, 2);
        if (@as(c_int, fade.unk3) < step) step = fade.unk3;
        var delta: c_int = @as(c_int, a.unk17E8.unk2) -% @as(c_int, fade.unk2);
        if (delta != 0) {
            delta *%= step;
            delta = @divTrunc(delta, @as(c_int, fade.unk3));
            fade.unk2 = psx.conv(u8, @as(c_int, fade.unk2) +% delta);
        }
        delta = @as(c_int, a.unk17E8.unk1) -% @as(c_int, fade.unk1);
        if (delta != 0) {
            delta *%= step;
            delta = @divTrunc(delta, @as(c_int, fade.unk3));
            fade.unk1 = psx.conv(u8, @as(c_int, fade.unk1) +% delta);
        }
        fade.unk3 = psx.conv(u8, @as(c_int, fade.unk3) -% step);
    }
}

/// func_800AB4F0 (the texture load) unless an earlier slot (0..i-1, empty
/// ones skipped on the PC) uses the same texture (unk6E6).
fn loadTexture(entries: *[17]?*Actor, i: c_int, a: *Actor) void {
    var step: c_int = 0;
    if (i > 0) {
        const texture = a.unk6E6;
        while (true) {
            const previous = psx.at(entries, step).*;
            step += 1;
            if (previous) |p| {
                if (p.unk6E6 == texture) return;
            }
            if (step >= i) break;
        }
    }
    c.func_800AB4F0(@ptrCast(a));
}

/// The actor's position moved by its unk6F4 offset (`sign` 1) or back (-1).
fn shift(a: *Actor, sign: c_int) void {
    const o = &a.unk6F4;
    a.unk0.position.vx = psx.conv(i16, @as(c_int, @as(u16, @bitCast(a.unk0.position.vx))) +% sign *% @as(c_int, o.unk0));
    a.unk0.position.vy = psx.conv(i16, @as(c_int, @as(u16, @bitCast(a.unk0.position.vy))) +% sign *% @as(c_int, o.unk1));
    a.unk0.position.vz = psx.conv(i16, @as(c_int, @as(u16, @bitCast(a.unk0.position.vz))) +% sign *% @as(c_int, o.unk2));
}

/// orig: func_800AEF94 (src/BATTLE/BATTLE.PRG/44F14.c)
/// Per frame, after func_800AECA0(camera): each live, visible actor slot's
/// skeleton, colour and texture work (func_800AAE9C, AB788, AB9A4, the fade,
/// AB4F0 once per texture, AC540, AF6E8 or AEAE8) and its bones on the camera
/// or on another actor's bone (an empty slot: the C's static zero matrix, and
/// a log line once, on the PC), with a linked actor (unk17FD) updated too and
/// the actor drawn offset; then the weapons on their actors' bones, then the
/// room objects (D_800F45E0) at depth + 16, the selected one animated.
pub fn func_800AEF94(camera: [*c]v.MATRIX) callconv(.c) void {
    c.func_800AECA0(@ptrCast(camera));
    var i: c_int = 0;
    const entries = v.g.D_800F4538();
    while (true) {
        if (psx.at(entries, i).*) |actor| {
            if (actor.unk0.nBones != 0 and word(actor, 8).* & 1 == 0 and actor.unk0.unk13 != 0xFC) {
                c.func_800AAE9C(@ptrCast(actor));
                if (actor.unk0.unk64.unk0 != 0) c.func_800AB788(@ptrCast(actor), null, 0);
                if (word(actor, 8).* & 0xF00 != 0) c.func_800AB9A4(@ptrCast(actor));
                const flags = word(actor, 8).*;
                word(actor, 8).* = flags & 0x7FFF_FFFF;
                if (flags & 4 != 0) {
                    fadeColour(actor);
                    loadTexture(entries, i, actor);
                    if (word(actor, 0x5AC).* & 3 != 0) c.func_800AC540(i, @ptrCast(actor));
                    if (v.g.vs_gametime_tickspeed().* == 4) actor.unk5B2 *%= 2;
                    c.func_800AF6E8(@ptrCast(actor));
                    actor.unk5B2 = 1;
                    actor.unk5B2 = psx.conv(u8, ((word(actor, 0x5B0).* >> 3) & 1) +% 1);
                } else {
                    c.func_800AEAE8(@ptrCast(actor));
                }
                if (word(actor, 8).* & 0x2000 != 0) {
                    if (psx.at(entries, actor.unk0.unk12).*) |parent| {
                        c.func_800B28A8(@ptrCast(actor), @ptrCast(psx.at(&parent.bones, actor.unk0.unk13)), 0);
                    } else {
                        // The C's block-scope statics (globalised by the build).
                        const reported = v.g.func_800AEF94__reported();
                        const before = reported.*;
                        reported.* = before +% 1;
                        if (before == 0) {
                            var line: [96]u8 = undefined;
                            _ = c.sprintf(&line, "[actor] slot %d is attached to bone %d of empty slot %d", i,
                                @as(c_int, actor.unk0.unk13), @as(c_int, actor.unk0.unk12));
                            c.pc_log(&line);
                        }
                        c.func_800B28A8(@ptrCast(actor), @ptrCast(v.g.func_800AEF94__zero()), 0);
                    }
                } else {
                    c.func_800B28A8(@ptrCast(actor), @ptrCast(camera), 0);
                    const attachmentId = actor.unk17FD;
                    if (attachmentId != 0) {
                        if (attachmentId >= 2) {
                            const attached = psx.at(entries, actor.unk17FD).*.?;
                            c.func_800AAE9C(@ptrCast(attached));
                            if (attached.unk0.unk64.unk0 != 0) c.func_800AB788(@ptrCast(attached), null, 0);
                            if (word(attached, 8).* & 0xF00 != 0) c.func_800AB9A4(@ptrCast(attached));
                            if (word(actor, 8).* & 4 != 0) {
                                fadeColour(attached);
                                loadTexture(entries, i, attached);
                            }
                        }
                        depth().* +%= @as(c_int, actor.unk6F4.unk3) *% 4;
                        shift(actor, 1);
                        c.func_800B002C(@ptrCast(actor), @as(c_int, actor.unk17FD));
                        c.func_800B28A8(@ptrCast(actor), @ptrCast(camera), actor.unk17FD);
                        depth().* -%= @as(c_int, actor.unk6F4.unk3) *% 4;
                        shift(actor, -1);
                    }
                }
                if (actor.unk63C >= 0x80 and actor.unk5CC != 0) {
                    word(actor, 8).* = word(actor, 8).* | 0x8000_0000;
                }
            }
        }
        i += 1;
        if (i >= 0x11) break;
    }

    i = 0;
    const weapons = v.g.vs_battle_wepModels();
    while (true) {
        if (psx.at(weapons, i).*) |weapon| {
            if (word(weapon, 8).* & 0x10 != 0 and weapon.unkD != 0) {
                if (psx.at(entries, @as(u8, @truncate(word(weapon, 8).*)) & 0xF).*) |actor| {
                    const flags = word(actor, 8).*;
                    if (flags & 1 == 0 and flags & 2 != 0 and flags & 0x1000 == 0) {
                        c.func_800B217C(@ptrCast(weapon), @ptrCast(psx.at(&actor.bones, weapon.unkD)));
                    }
                }
            }
        }
        i += 1;
        if (i >= 0x14) break;
    }

    i = 0;
    const objects = v.g.D_800F45E0();
    depth().* +%= 0x10;
    while (true) {
        if (psx.at(objects, i).*) |object| {
            if (object.unk0 != 0) {
                if ((i == v.g.D_800F4B19().* and v.g.D_800F4B18().* == 0) or object.unk1A == 0xF7) {
                    const tick = v.g.vs_gametime_tickspeed().*;
                    c.func_800B0908(@ptrCast(object), (tick +% @as(c_int, @bitCast(@as(c_uint, @bitCast(tick)) >> 31))) >> 1);
                }
                const packedPosition = psx.byteAt(c_int, &object.unk1C, 0).*;
                const offset = objectOffset();
                offset.vz = object.unk20;
                const modelHeight: c_uint = @bitCast(psx.byteAt(c_int, psx.get(object.unk68).?, 0x50).*);
                psx.byteAt(c_int, offset, 0).* = packedPosition;
                offset.vy = psx.conv(i16, @as(c_int, @as(u16, @bitCast(offset.vy))) +% @divTrunc(@as(c_int, @bitCast(modelHeight)), 2));
                if (object.unk64.unk0 != 0) c.func_800AB788(null, @ptrCast(object), 0);
                if (word(object, 8).* & 0xF00 != 0) c.func_800AB9A4(@ptrCast(object));
                c.func_800B1A68(@ptrCast(object), @ptrCast(camera));
            }
        }
        i += 1;
        if (i >= 0x10) break;
    }
    v.g.D_800F4B18().* = 0;
    depth().* -%= 0x10;
    v.g.D_800E9308().* +%= 1;
}
