#ifndef ROOD_RENDER_STATE_H
#define ROOD_RENDER_STATE_H
#include <stdint.h>

/* Game-visible packet IDs, not renderer history. Storage belongs to the host
 * even in split builds; snapshot resume/rewind restore it and reload leaves it
 * alone. Never reset this allocator when invalidating presentation caches. */
extern uint32_t pc_render_next_id;
#endif
