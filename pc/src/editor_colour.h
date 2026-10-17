/* The colour popover (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * decision 7), opened from a light's, the sun's or a fog volume's swatch in the view: a
 * Temperature tab (a Kelvin strip with named stops and a magenta-green tint)
 * and a Colour tab (a hue wheel and value); under both the room's palette
 * (the colours its lights use), recent colours and Hex / RGB. It remembers the
 * last tab per light type and the recent colours in the user settings
 * (editor_colour_tabs, editor_recent_colours: preferences, not undo steps).
 * Each change is one undo step ("Edit Colour"). Editor internals. */
#ifndef ROOD_EDITOR_COLOUR_H
#define ROOD_EDITOR_COLOUR_H
#include <stdio.h>
#include <string>

#include "editor_selection.h"
#include "light_prefabs.h"

namespace rood_colour {
enum Tab { COLOUR_TAB_TEMPERATURE, COLOUR_TAB_WHEEL };
void open(const rood_selection::Ref& target, float x, float y); /* a light or the sun, by the swatch at (x, y) */
bool is_open();
void close();
void draw(); /* each overlay frame (dev_overlay.cpp), after the view */
/* The eyedropper armed: this click in the view picks the colour under it
 * (on the next presented image); true when it took the click. */
bool viewport_click(float x, float y);
bool cancel(); /* Escape: the eyedropper disarmed, else the popover closed; true when either */
std::string state_json(); /* RPC editor_handles: "colour" */
/* RPC editor_handles {action "colour"}: open (bool: on the selection's
 * primary), tab ("temperature" | "colour"), kelvin and tint, hex, rgb,
 * palette (index), recent (index). False and *error when refused. */
bool rpc(const prefab::Json& params, std::string* error);
/* Settings lines (project_settings.cpp). */
bool parse_setting(const char* key, const char* rest);
void write_settings(FILE* f);
} // namespace rood_colour
#endif
