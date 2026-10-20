//! Starting an actor's animation (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0"); // its D_800F4538_t is opaque: view pointers go to it through @ptrCast

/// orig: func_800A0204 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Starts animation `animation` (mapped by the actor's stance and weapon) on actor `actorId`, blending over `blendFrames`; -1 for an empty slot.
pub fn startAnimation(actorId: c_int, animation: c_int, _: c_int, blendFrames_: c_int) callconv(.c) c_int {
    var blendFrames = blendFrames_;
    var frames: [*c]c.u_short = undefined;

    if (animation == 0) {
        return 0;
    }
    // D_800F4538[actorId]: the C does not check the id.
    const actor = psx.at(v.g.D_800F4538(), actorId).* orelse return -1;
    if (psx.byteAt(c_int, actor, 0x5AC).* & 0x1000000 != 0) {
        return 0;
    }
    psx.setBits(actor, "unk5AC_16", 0);
    if (psx.byteAt(c_int, actor, 0x5AC).* & 0x800000 != 0) {
        c.func_800AE47C(@ptrCast(actor));
    }
    psx.setBits(actor, "unk5AC_23", 0);
    var keepFlag15: c_int = 0;
    if (animation == 25 or animation == 26) {
        keepFlag15 = 1;
    }
    if (keepFlag15 == 0) {
        psx.setBits(actor, "unk5AC_15", 0);
    }
    actor.unk1802[2] = 0;
    actor.unk1802[6] = 0;
    _ = c.func_800AAD4C(actorId, 1, 1, 1);
    c.func_800AC500(@ptrCast(actor));
    if (psx.byteAt(c_int, actor, 0x5AC).* & 0x8000 != 0 and keepFlag15 == 0) {
        psx.setBits(actor, "unk5AC_15", 0);
    }
    var anim = animation;
    psx.setBits(actor, "unk5B0_3", 0);

    if (anim < 100) {
        if (psx.getBits(&actor.unk0, "unkA_5") != 0) {
            if (anim == 1 or anim == 6) {
                anim = 8;
            }
        } else if (psx.getBits(&actor.unk0, "unkA_0") == 3) {
            if (anim == 1) {
                anim = 48;
                if (psx.getBits(&actor.unk0, "unkA_6") != 0) {
                    anim = 35;
                }
            }
        } else if (psx.getBits(&actor.unk0, "unkA_6") != 0) {
            if (anim == 1) {
                anim = 31;
            } else if (anim == 13) {
                anim = 32;
            }
        }
        if (psx.getBits(&actor.unk0, "weaponDrawn") != 0) {
            anim +%= 100;
        } else if (psx.getBits(actor, "unk5AC_3") != 0) {
            if (anim == 1) {
                anim = 75;
            } else if (anim == 13) {
                anim = 84;
                if (actor.animationId == anim) {
                    return 0;
                }
            } else if (anim == 84) {
                if (actor.animationId == anim) {
                    return 0;
                }
            }
        }
    }

    psx.setBits(actor, "unk5AC_20", 0);
    setHeading: {
        if (psx.getBits(&actor.unk0, "weaponDrawn") == 0 and (anim == 36 or anim == 39)) {
            psx.setBits(&actor.unk0, "unkA_3", 1);
        } else if (psx.getBits(&actor.unk0, "weaponDrawn") == 0 and (anim == 41 or anim == 59)) {
            psx.setBits(&actor.unk0, "unkA_3", 2);
        } else {
            if (@as(*align(1) c_int, @ptrCast(&actor.unk0.unk34)).* == 0 and actor.unk0.unk34.vz == 0) {
                psx.setBits(&actor.unk0, "unkA_3", 0);
                psx.setBits(&actor.unk0, "unk9_6", 0);
            }
            break :setHeading;
        }
        actor.unk1800 = psx.conv(i16, (@as(c_int, actor.unk0.facing) +% actor.unk0.unk3E) & 0xFFF);
    }

    var found: c_int = undefined;
    retry: while (true) {
        found = c.func_800AD494(@ptrCast(actor), anim, &frames);
        if (found == 0 or frames == null) {
            psx.setBits(actor, "unk5AC_15", 0);
            if (anim == 0) {
                // `return;` in an int function: the object returns func_800AD494's result (%eax).
                return found;
            }
            anim = 0;
            continue :retry;
        }
        break;
    }

    actor.animationId = psx.conv(u8, anim);
    if (c.func_800BEB00() == 4 or actor.unk187C != found or @as(c_int, v.g.D_800F49DC().*) != actor.unk6E3) {
        actor.unk6E3 = v.g.D_800F49DC().*;
        actor.unk187C = found;
        actor.unk5BC = psx.conv(i16, frames[0]);
        c.func_800AD008(@ptrCast(actor), &actor.unkC54);
        actor.unk6E0 = 0;
        c.func_800A06B8(@ptrCast(actor), blendFrames);
        if (actor.unk63C >= 0x80) {
            blendFrames *%= 2;
        }
        if (blendFrames >= 2) {
            actor.unk5CD = psx.conv(u8, blendFrames);
            actor.unk5CC = 1;
        } else {
            actor.unk5CD = 0;
            actor.unk5CC = 0;
        }
    }
    return 0;
}
