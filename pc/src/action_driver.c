/* The action driver (dev RPC `action`): starts action N through the game's own entries, so the effect runs as in play.
 *
 * A player's action (a spell, a break art, an item) goes in by the entry the menus' target confirmation calls
 * (pc_action_start_player). What the menus leave in the battle state vs_battle_actionState when they call it is the action index (+8), the
 * targets they built and a few menu words; nothing else differs between two actions of the same kind. So the state is captured as
 * menu-driven actions call the entry (`capture`) and replayed with another action index: one state per kind, the action's type and
 * its shape of target (pc_action_kind); only a choice the entry takes (not a pending one it drops) is captured, and a break art is
 * replayed only for its own weapon category (its effect reads the weapon). The states are the host's (dev_rpc.c: they outlive a rewind
 * of the game's state, and `save` / `load` keep them in a file). An action of a kind never captured is refused.
 *
 * An actor's action (the base attack, an enemy's ability the actor commands run from its own list) goes in by
 * pc_action_start_actor with no captured state: {id, actor}. Other actions have their own entries: the chain and defence abilities
 * (type 2) are resolved as hits from the chain input, traps (type 7) by the room's panels, type 0 has no effect. */
#define STATE_BYTES 0x2C10
#define ACTION_BYTES 52
extern unsigned char* vs_battle_actionState;
extern unsigned char vs_main_actions[];
extern unsigned D_800F196C; /* the battle's mode: 1 free, 2 while an action runs */
extern unsigned char vs_battle_menuState;
extern void pc_action_start_player(int kind); /* 146C.c: vs_battle_commitSelectedAction, the entry the menus' target confirmation calls */
extern void pc_action_start_actor(int actor, int action); /* 146C.c: the start of an actor's action: the base attack's, an enemy's ability's */
extern void* vs_battle_actors[16];

/* dev_rpc.c */
extern int pc_action_host_armed(void);
extern void pc_action_host_store(int key, const void* state, int kind, int mode, int menu, int action);
extern const unsigned char* pc_action_host_get(int key, int action, int* kind, int* mode, int* menu);
extern int pc_action_host_take(int* actor);
extern void pc_action_host_started(void);

/* The kind of an action: its type (3 bits: spell, ability, break art, item, base attack, trap) and its shape of target (the high
 * nibble), the two things the menus' target state depends on. */
int pc_action_kind(int action)
{
    unsigned char byte = vs_main_actions[action * ACTION_BYTES + 2];
    return (byte >> 1 & 7) << 4 | byte >> 4;
}

/* commit_action.zig, as the entry starts. */
void pc_action_capture(const void* state, int kind, int mode, int menu)
{
    if (!pc_action_host_armed() || !state)
        return;
    /* A menu choice left pending (unk298C 0xFF) with a nonzero kind is dropped by the entry (it goes back to the actor's turn
     * and opens the top menu): not an action, and not a state to replay, which would step a menu that is not open. */
    if (kind != 0 && ((const unsigned char*)state)[0x298C] == 0xFF)
        return;
    int action = ((const unsigned char*)state)[8] | ((const unsigned char*)state)[9] << 8;
    if (action < 256)
        pc_action_host_store(pc_action_kind(action), state, kind, mode, menu, action);
}
/* The battle loop, where the player is free to act (next to the Room Browser's warp): 1 when the action started. */
int pc_action_poll(void)
{
    int actor, pending = pc_action_host_take(&actor), kind, mode, menu;
    if (pending < 0 || !vs_battle_actionState)
        return 0;
    if (actor >= 0) { /* an actor's action by the game's own entry (pc_action_start_actor), the targets chosen as it chooses them */
        if (!vs_battle_actors[actor])
            return 0;
        pc_action_host_started();
        pc_action_start_actor(actor, pending);
        return 1;
    }
    const unsigned char* state = pc_action_host_get(pc_action_kind(pending), pending, &kind, &mode, &menu);
    if (!state)
        return 0;
    for (int i = 0; i < STATE_BYTES; i++)
        vs_battle_actionState[i] = state[i];
    vs_battle_actionState[8] = (unsigned char)(pending & 255), vs_battle_actionState[9] = (unsigned char)(pending >> 8);
    D_800F196C = (unsigned)mode, vs_battle_menuState = (unsigned char)menu; /* as the menus left them */
    pc_action_host_started();
    pc_action_start_player(kind);
    return 1;
}
