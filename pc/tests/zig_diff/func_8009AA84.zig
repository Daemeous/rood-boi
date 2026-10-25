//! zig_diff spec: func_8009AA84 -> func_8009AA84.func_8009AA84 (30DB0.c).
//! Inputs: the 20 wepModels slots are each null or an arena model with
//! nBones 0 or nonzero, actorId 0 or nonzero, modelId from {1,2,3} (so
//! matches are common), a texture slot (19 only when not wide), a wide
//! texture bit and a fresh offsets block. Half the time another slot is
//! forced to a model with actorId 0 and the same modelId as the index's
//! model, so the scan takes the `actorId` test's false exit and goes on.
//! Textures get refCount 0, 1, 255 or random, so the u8 decrement wraps.
//! The index is 0..19 (never out of the array, since the C would read
//! other globals as pointers). The index-4 edge is covered for `index >= 4`.
//! The `obj->index < 4` test sits under `index >= 4`, so its true arm is
//! dead and the compiler folds it. vs_main_freeHeap is a void recording
//! stub: the C never reads back what it frees.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009AA84));

pub const classes = [_][:0]const u8{
    "null-model",
    "no-bones",
    "no-actor",
    "actor-low-index",
    "actor-found",
    "actor-not-found",
};

fn freshModel(ctx: *zd.Ctx, actor: u4, model_id: u8) *v.vs_battle_wepModels_t {
    const m = ctx.alloc(v.vs_battle_wepModels_t);
    m.nBones = if (ctx.chance(8)) 0 else ctx.edge(u8, &.{ 1, 2, 255 });
    m.modelId = model_id;
    psx.setBits(m, "actorId", actor);
    const wide = ctx.chance(3);
    psx.setBits(m, "isWideTexture", @intFromBool(wide));
    m.texSlot = if (wide) ctx.range(u8, 0, 18) else ctx.range(u8, 0, 19);
    psx.put(&m.offsets, ctx.alloc(v._wepModelOffsets));
    return m;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const models = v.g.vs_battle_wepModels();
    const textures = v.g.vs_battle_wepTextures();

    for (textures) |*t| {
        if (ctx.chance(2)) t.refCount = ctx.edge(u8, &.{ 0, 1, 255 });
    }

    const index: c_int = ctx.range(c_int, 0, 19);
    const idx: usize = @intCast(index);

    for (models) |*slot| {
        if (ctx.chance(4)) {
            slot.* = null;
            continue;
        }
        const actor: u4 = if (ctx.chance(4)) 0 else ctx.edge(u4, &.{ 1, 2, 15 });
        slot.* = freshModel(ctx, actor, ctx.edge(u8, &.{ 1, 2, 3 }));
    }

    // Force the actorId-false exit: another slot, not the index, holds a
    // model with actorId 0 and the index's modelId.
    if (models[idx]) |m| {
        if (ctx.chance(2)) {
            const j = (idx + ctx.range(usize, 1, 19)) % 20;
            psx.setBits(m, "actorId", ctx.edge(u4, &.{ 1, 2, 15 }));
            models[j] = freshModel(ctx, 0, m.modelId);
        }
    }

    return .{index};
}

fn othersHaveMatch(models: *[20]?*v.vs_battle_wepModels_t, index: usize, m: *v.vs_battle_wepModels_t) bool {
    for (0..20) |i| {
        if (i == index) continue;
        const o = models[i] orelse continue;
        if (psx.getBits(o, "actorId") != 0 and o.modelId == m.modelId) return true;
    }
    return false;
}

pub fn classify(args: Args) usize {
    const index: usize = @intCast(args[0]);
    const models = v.g.vs_battle_wepModels();
    const m = models[index] orelse return 0;
    if (m.nBones == 0) return 1;
    if (psx.getBits(m, "actorId") == 0) return 2;
    if (index < 4) return 3;
    return if (othersHaveMatch(models, index, m)) 4 else 5;
}
