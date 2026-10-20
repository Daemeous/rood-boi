#include "render_state.h"
#include "snapshot.h"

/* Explicit host unit: included in the pinned saved-state layout and its
 * compatibility key. The reloadable engine only imports this symbol. */
uint32_t pc_render_next_id PC_SAVED_STATE = 1;
