/* The editor's side of the room environment (environment.cpp): its items
 * in the lighting layers and undo snapshots. Editor internals. */
#ifndef ROOD_ENVIRONMENT_INTERNAL_H
#define ROOD_ENVIRONMENT_INTERNAL_H
#include <string>

#include "lighting_layers.h"

namespace rood_env {
/* No sun, no sky (a room's layers are applied after this). */
void reset();
/* An `env sun` / `env sky` item into the room; false for other items. */
bool apply_item(const lighting::Item& it);
/* The room's env items (those it has) into a state. */
void add_items(lighting::State& s);
/* The items as definition lines (undo snapshots), and back. */
std::string text();
void set_text(const std::string& text);
} // namespace rood_env
#endif
