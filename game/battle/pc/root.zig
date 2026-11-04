//! D2 ports: the PC C in pc/src that implements BATTLE's PS1 assembly (PORTING.md §20).
//! One section per oracle file, each with its own subsystem in ownership.toml;
//! a port adds its `pub const` line under its file's header, in address order.

// pc/src/actor_blend.c

// pc/src/actor_cleanup.c
pub const reset_actor_values = @import("reset_actor_values.zig");
pub const clear_grid_cell_flag = @import("clear_grid_cell_flag.zig");
pub const release_actor_tile = @import("release_actor_tile.zig");
pub const unload_all_actors = @import("unload_all_actors.zig");
pub const unload_actor = @import("unload_actor.zig");
pub const room_height_limit = @import("room_height_limit.zig");

// pc/src/actor_packets.c
pub const build_actor_packets = @import("build_actor_packets.zig");
pub const build_blended_actor_packets = @import("build_blended_actor_packets.zig");

// pc/src/actor_shadow.c
pub const shadow_actor = @import("shadow_actor.zig");

// pc/src/actor_update.c
pub const route_target_state = @import("route_target_state.zig");
pub const free_actor_route = @import("free_actor_route.zig");
pub const start_actor_route = @import("start_actor_route.zig");
pub const fill_actor_scratch = @import("fill_actor_scratch.zig");
pub const update_actor_navigation = @import("update_actor_navigation.zig");

// pc/src/actor_vertices.c
pub const project_object_vertices = @import("project_object_vertices.zig");

// pc/src/battle_gte.c
pub const facing_cosine = @import("facing_cosine.zig");

// pc/src/battle_helpers.c
pub const send_actor_value = @import("send_actor_value.zig");
pub const unk8e_at_least_20 = @import("unk8e_at_least_20.zig");
pub const tick_action_timer = @import("tick_action_timer.zig");
pub const tick_state_timers = @import("tick_state_timers.zig");
pub const squared_distance = @import("squared_distance.zig");
pub const merge_actor_flags = @import("merge_actor_flags.zig");
pub const set_actor_stat_pair = @import("set_actor_stat_pair.zig");
pub const room_height_tile = @import("room_height_tile.zig");
pub const sprite_packet = @import("sprite_packet.zig");
pub const choose_movement_mode = @import("choose_movement_mode.zig");
pub const copy_candidate = @import("copy_candidate.zig");
pub const candidate_record = @import("candidate_record.zig");
pub const candidate_sort = @import("candidate_sort.zig");
pub const copy_actor_stats = @import("copy_actor_stats.zig");
pub const actor_list = @import("actor_list.zig");
pub const actor_target_flags = @import("actor_target_flags.zig");
pub const mark_target_hostile = @import("mark_target_hostile.zig");
pub const zero_fill = @import("zero_fill.zig");
pub const navigation_floor = @import("navigation_floor.zig");
pub const actor_conditions = @import("actor_conditions.zig");
pub const ai_driver = @import("ai_driver.zig");
pub const actor_disposition = @import("actor_disposition.zig");

// pc/src/battle_motion.c
pub const look_request = @import("look_request.zig");
pub const motion_start = @import("motion_start.zig");
pub const motion_request = @import("motion_request.zig");
pub const turn_request = @import("turn_request.zig");

// pc/src/battle_movement.c
pub const leave_encounter_by_id = @import("leave_encounter_by_id.zig");
pub const leave_encounter = @import("leave_encounter.zig");
pub const join_encounter = @import("join_encounter.zig");
pub const tile_enterable = @import("tile_enterable.zig");
pub const step_allowed = @import("step_allowed.zig");
pub const blocking_actor = @import("blocking_actor.zig");
pub const approach_actor = @import("approach_actor.zig");
pub const enter_movement_mode = @import("enter_movement_mode.zig");
pub const wander_own_range = @import("wander_own_range.zig");
pub const wander_around = @import("wander_around.zig");
pub const start_walk_to_tile = @import("start_walk_to_tile.zig");
pub const start_patrol = @import("start_patrol.zig");
pub const flee_from_hostiles = @import("flee_from_hostiles.zig");
pub const aim_at_actor = @import("aim_at_actor.zig");
pub const finish_stop_request = @import("finish_stop_request.zig");
pub const decide_movement = @import("decide_movement.zig");
pub const can_engage_target = @import("can_engage_target.zig");
pub const resolve_flagged_cell = @import("resolve_flagged_cell.zig");
pub const find_actor_by_relation = @import("find_actor_by_relation.zig");
pub const target_needs_engagement = @import("target_needs_engagement.zig");
pub const candidate_allowed = @import("candidate_allowed.zig");
pub const no_hostile_near_target = @import("no_hostile_near_target.zig");
pub const candidate_startable = @import("candidate_startable.zig");
pub const approach_point = @import("approach_point.zig");
pub const approach_point_if_approaching = @import("approach_point_if_approaching.zig");
pub const axis_passability = @import("axis_passability.zig");
pub const behind_facing = @import("behind_facing.zig");
pub const climb_target = @import("climb_target.zig");
pub const command_byte_request = @import("command_byte_request.zig");
pub const cry_request = @import("cry_request.zig");
pub const floor_height = @import("floor_height.zig");
pub const floor_height_at_position = @import("floor_height_at_position.zig");
pub const floor_height_of_tile = @import("floor_height_of_tile.zig");
pub const ground_height = @import("ground_height.zig");
pub const ground_height_at_position = @import("ground_height_at_position.zig");
pub const ground_height_of_tile = @import("ground_height_of_tile.zig");
pub const issue_command = @import("issue_command.zig");
pub const leave_engaged = @import("leave_engaged.zig");
pub const reaction_pick = @import("reaction_pick.zig");
pub const record_step_tile = @import("record_step_tile.zig");
pub const reset_step_plan = @import("reset_step_plan.zig");
pub const scratch_random = @import("scratch_random.zig");
pub const start_step_plan = @import("start_step_plan.zig");
pub const step_blocked = @import("step_blocked.zig");
pub const step_clamped = @import("step_clamped.zig");
pub const tile_distance_squared = @import("tile_distance_squared.zig");
pub const reaction_command_11 = @import("reaction_command_11.zig");
pub const reaction_command_12 = @import("reaction_command_12.zig");
pub const steer_to_tile = @import("steer_to_tile.zig");
pub const free_movement = @import("free_movement.zig");
pub const movement_flags = @import("movement_flags.zig");
pub const room_flag = @import("room_flag.zig");
pub const mark_cell_occupied = @import("mark_cell_occupied.zig");
pub const alert_actor = @import("alert_actor.zig");
pub const remember_target = @import("remember_target.zig");
pub const beyond_reach = @import("beyond_reach.zig");
pub const aim_at_point = @import("aim_at_point.zig");
pub const movement_dispatch = @import("movement_dispatch.zig");
pub const release_if_idle = @import("release_if_idle.zig");
pub const hold_actor = @import("hold_actor.zig");
pub const clear_actor_hold = @import("clear_actor_hold.zig");
pub const mark_cell_blocked = @import("mark_cell_blocked.zig");
pub const tile_occupancy = @import("tile_occupancy.zig");
pub const lowest_floor = @import("lowest_floor.zig");
pub const trace_attack = @import("trace_attack.zig");
pub const release_room_flag = @import("release_room_flag.zig");
pub const clear_actor_targets = @import("clear_actor_targets.zig");
pub const alloc_detour_record = @import("alloc_detour_record.zig");

// pc/src/effect_particles.c

// pc/src/map_view.c

// pc/src/navigation.c
pub const exit_links = @import("exit_links.zig");
pub const temporary_heap_pass = @import("temporary_heap_pass.zig");
pub const navigation_grid = @import("navigation_grid.zig");
pub const tile_height = @import("tile_height.zig");

// pc/src/room_geometry.c

// pc/src/room_lighting.c
pub const room_light_at = @import("room_light_at.zig");
pub const room_light_triangle = @import("room_light_triangle.zig");

// pc/src/room_render.c

// pc/src/weapon_packets.c
pub const build_weapon_packets = @import("build_weapon_packets.zig");
