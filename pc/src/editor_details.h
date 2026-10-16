/* Details (dev_overlay.cpp's Details tab; editor_details.cpp): the
 * selection's properties drawn from the editor's schema (editor_schema.h),
 * and Houdini's value ladder on its numbers; the same for a standalone
 * kind's object anywhere in the dev tools (draw_kind). Editor internals. */
#ifndef ROOD_EDITOR_DETAILS_H
#define ROOD_EDITOR_DETAILS_H
#include <string>

namespace rood_schema {
struct Kind;
}

namespace rood_details {
void draw(); /* the Details tab's body */
/* A standalone kind's object (editor_schema.h add_kind, object KIND_NONE)
 * drawn as Details draws the selection: its categories, a search box of its
 * own, the reset arrows, the value ladder, its tools; one undo step per
 * edit. RPC editor_schema {action "ui"} reports it under "standalone". */
void draw_kind(const rood_schema::Kind& k, int index);
/* draw_kind under a collapsing header (closed at first) for the kind named
 * `kind` (nothing when there is none): a panel's one-line hook. */
void kind_section(const char* kind, const char* title, int index = 0);
/* RPC editor_schema {action "ui", open: kind}: kind_section opens that kind's header next frame and
 * scrolls the panel to it. */
void open_section(const std::string& kind);
void ladder_frame(); /* a value ladder being dragged follows the mouse (every overlay frame) */
bool ladder_cancel(); /* Escape: the values back; true when a ladder was open */
std::string state_json(); /* RPC editor_schema {action "ui"} */
void set_search(const std::string& text);
void set_kind_search(const std::string& kind, const std::string& text); /* draw_kind's, by kind */
} // namespace rood_details
#endif
