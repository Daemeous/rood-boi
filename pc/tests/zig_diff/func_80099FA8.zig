//! zig_diff spec: func_80099FA8 -> func_80099FA8.func_80099FA8 (30DB0.c).
//! Inputs: an objData in the arena whose index is an edge (-1, -128, 0, 3,
//! 4, 5, 19, 20, 127) and whose modelId is a small model id, that id plus
//! 256 (the unsigned-cast slip), or a negative value. The 20-slot
//! vs_battle_wepModels table is filled with random nulls or arena models
//! (actorId 0-15 with its sign bit, modelId 0-3 or 255). Matches are planted
//! at the objData's own slot (the self-skip) and at a random slot (so the
//! search, the `index < 4` test and the bound at 19 all decide the result).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80099FA8));

pub const classes = [_][:0]const u8{ "index-negative", "index-4", "index-0-19", "index-20-plus" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const od = ctx.alloc(v.vs_battle_objectData);
    od.index = ctx.pick(i8, &.{ -1, -128, 0, 3, 4, 5, 19, 20, 127 });
    od.modelId = ctx.pick(i16, &.{ 0, 1, 2, 3, 256, 257, -1, 0x7FFF, -256 });

    // The C reads all 20 slots of the table (unless it returns early), so
    // every slot is set: null, or a valid arena model.
    const models = v.g.vs_battle_wepModels();
    var k: usize = 0;
    while (k < 20) : (k += 1) {
        if (ctx.chance(4)) {
            models[k] = null;
        } else {
            const m = ctx.alloc(v.vs_battle_wepModels_t);
            psx.setBits(m, "actorId", ctx.pick(u4, &.{ 0, 0, 1, 8, 15 }));
            m.modelId = ctx.pick(u8, &.{ 0, 1, 2, 3, 255 });
            models[k] = m;
        }
    }

    // A match at the objData's own slot: the C must skip it (self-skip).
    if (od.index >= 0 and od.index < 20 and ctx.chance(3)) {
        if (models[@intCast(od.index)]) |own| {
            own.modelId = psx.conv(u8, od.modelId);
            psx.setBits(own, "actorId", ctx.range(u4, 1, 15));
        }
    }

    // A match at a random slot, with actorId 0 or 8 so the `index < 4`
    // test decides it.
    if (ctx.chance(3)) {
        const other = ctx.range(usize, 0, 19);
        if (models[other]) |m| {
            m.modelId = psx.conv(u8, od.modelId);
            psx.setBits(m, "actorId", ctx.pick(u4, &.{ 0, 0, 8 }));
        }
    }

    return .{od};
}

pub fn classify(args: Args) usize {
    const idx = args[0].?.index;
    if (idx < 0) return 0;
    if (idx == 4) return 1;
    if (idx < 20) return 2;
    return 3;
}
