//! Steps an idle or walking actor one tile along its facing (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009E634 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Requests a one-tile step along the actor's facing for its current animation, unless the actor is skipping or the animation excludes it.
pub fn func_8009E634(arg0: ?*v.D_800F4538_t) callconv(.c) void {
    const actor = arg0.?;
    var var_a0: c_int = 1;

    if (actor.unk0.unkF == 0) {
        const anim: c_int = actor.animationId;
        if (psx.conv(c_uint, anim -% 0x4F) >= 4) {
            if (psx.conv(c_uint, anim -% 0x47) >= 4 and anim != 0x4C) {
                return;
            }
        } else {
            var_a0 = 2;
        }

        var temp_v1_2: c_int = @as(c_int, actor.unk0.facing) +% 0x200;
        temp_v1_2 &= 0xFFF;
        temp_v1_2 = @divTrunc(temp_v1_2, 1024);

        var var_a2: c_int = 0;
        var var_a3: c_int = 0;

        if (temp_v1_2 < 2) {
            var_a0 = -%var_a0;
        }

        if ((temp_v1_2 & 1) != 0) {
            var_a2 = var_a0;
        } else {
            var_a3 = var_a0;
        }
        c.func_800E4C28(
            @as(c_int, actor.unk0.currentTileX) +% var_a2,
            @as(c_int, actor.unk0.currentTileZ) +% var_a3,
            var_a2,
            var_a3,
        );
    }
}
