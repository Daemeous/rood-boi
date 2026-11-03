//! D2 ports: the PC C in pc/src that implements BATTLE's PS1 assembly (PORTING.md §20).
//! One section per oracle file, each with its own subsystem in ownership.toml;
//! a port adds its `pub const` line under its file's header, in address order.

// pc/src/actor_blend.c

// pc/src/actor_cleanup.c

// pc/src/actor_packets.c

// pc/src/actor_shadow.c

// pc/src/actor_update.c

// pc/src/actor_vertices.c

// pc/src/battle_gte.c

// pc/src/battle_helpers.c
pub const send_actor_value = @import("send_actor_value.zig");
pub const unk8e_at_least_20 = @import("unk8e_at_least_20.zig");
pub const tick_action_timer = @import("tick_action_timer.zig");
pub const tick_state_timers = @import("tick_state_timers.zig");
pub const squared_distance = @import("squared_distance.zig");
pub const merge_actor_flags = @import("merge_actor_flags.zig");
pub const set_actor_stat_pair = @import("set_actor_stat_pair.zig");

// pc/src/battle_motion.c
pub const look_request = @import("look_request.zig");

// pc/src/battle_movement.c

// pc/src/effect_particles.c

// pc/src/map_view.c

// pc/src/navigation.c

// pc/src/room_geometry.c

// pc/src/room_lighting.c

// pc/src/room_render.c

// pc/src/weapon_packets.c
