/* Details (dev_overlay.cpp's Details tab; editor_details.cpp): the
 * selection's properties drawn from the editor's schema (editor_schema.h),
 * and Houdini's value ladder on its numbers. Editor internals. */
#ifndef ROOD_EDITOR_DETAILS_H
#define ROOD_EDITOR_DETAILS_H
#include <string>

namespace rood_details {
void draw(); /* the Details tab's body */
void ladder_frame(); /* a value ladder being dragged follows the mouse (every overlay frame) */
bool ladder_cancel(); /* Escape: the values back; true when a ladder was open */
std::string state_json(); /* RPC editor_schema {action "ui"} */
void set_search(const std::string& text);
} // namespace rood_details
#endif
