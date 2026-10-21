/* The photo mode foundation's host parts (photo.h): photo.cpp (the pause, its renders, the RPC, what
 * every part shares) and one file per capability (photo_camera.cpp, photo_ui.cpp, photo_capture.cpp,
 * photo_input.cpp), each registering a Part so that releasing an owner, the safety, the RPC and an
 * engine reload reach it. C++, the game thread. */
#ifndef ROOD_PHOTO_INTERNAL_H
#define ROOD_PHOTO_INTERNAL_H
#include <string>
#include <vector>

namespace photo {
struct Part {
    const char* name;
    void (*release)(const std::string& owner);     /* everything the owner holds of this part */
    void (*owners)(std::vector<std::string>& out); /* who holds any (the safety, an engine reload) */
    void (*reset)();                               /* back to rest (an engine reload; after the releases) */
    void (*json)(std::string& out);                /* RPC photo: ,"key":value fields */
    void (*params)(const char* json);              /* RPC photo: its tunables */
};
std::vector<Part>& parts();
struct Register {
    explicit Register(const Part& p) { parts().push_back(p); }
};

void copy_out(char* out, int size, const std::string& text);
double now_seconds();
bool contains(const std::vector<std::string>& v, const std::string& s);
/* An owner's hold taken back without its asking (the safety, an engine reload): told to its plugin. */
void notify(const std::string& owner, const char* what, const char* reason);
/* An integer field of a JSON object's text ("key": n): true and *out. */
bool json_int(const char* json, const char* key, int* out);
std::string json_quote(const std::string& s);

/* The pause, for the parts. */
bool paused();  /* asked for or held (renders included) */
bool settled(); /* not held, or held with the picture as it is to be: no render waiting or running */
unsigned game_frames(); /* game frames completed (pc_battle_frame_count) */

/* photo_ui.cpp, for a capture: the picture's rectangle cropped to the format (x, y, w, h, top-left
 * origin), and the game frame the hidden layers last changed at. */
void format_rect(const int view[4], int out[4]);
unsigned hide_changed_frame();
} // namespace photo
#endif
