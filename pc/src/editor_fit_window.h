/* "Fit rect light to window" (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * the rect light's "Later: fit to window"; the de plan's decision 17): armed,
 * a click on a flat textured room surface (a window pane: the connected
 * triangles in its plane with its texture, rood_math::fit_pane) sizes and
 * places a rect light on it, offset in front of it and facing out; the
 * selected rect light, or a new one in a daylight colour. One undo step.
 * Settings keys editor_fit_window_offset (default 2 units) and
 * editor_fit_window_kelvin (default 5600 K). Editor internals; RPC
 * editor_handles {action "fit_window" | "fit_arm"}. */
#ifndef ROOD_EDITOR_FIT_WINDOW_H
#define ROOD_EDITOR_FIT_WINDOW_H
#include <stdio.h>
#include <string>

#include "editor_selection.h"

namespace rood_fit {
extern float offset, kelvin; /* tunable: the settings keys, the tool's popup, RPC */
void arm(const rood_selection::Ref& light); /* the next click fits that rect light; an empty ref: a new one */
bool armed();
rood_selection::Ref target();
bool cancel(); /* Escape: true when it was armed */
bool viewport_click(float x, float y); /* armed: this click fits (true: it took the click) */
void hint(void* draw_list); /* armed: the pane under the pointer outlined, a label */
void options(); /* the offset and the colour (a popup's body) */
/* Fit at window pixel (x, y): `light` (a rect light) or a new one; its index, or -1 and *message. */
int fit_at(float x, float y, const rood_selection::Ref& light, std::string* message);
bool parse_setting(const char* key, const char* rest);
void write_settings(FILE* f);
std::string state_json();
} // namespace rood_fit
#endif
