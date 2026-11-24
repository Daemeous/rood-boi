// GIM pictures drawn as real text. The game shows some text as pictures (data/GIM: the intro quote, the briefing pages, the staff credit cards:
// pc/tools/gim_decode.py). With the modern fonts on (ui_text font) and the player's own text pack for a picture (pc/user/gim_text/<FILE>.txt, written by
// pc/tools/gim_text_pack.py from the player's disc, never in the repository) the picture's drawers (vs_render_drawGimTiles, the scanline wipe) leave what its pack
// lines' boxes cover out of the picture and make a carrier sprite item per line at the place where they draw the picture (pc_gim_text_carry); ui_text.c sets the line
// in the font as one glyph image coloured by the fill, and gpu_hw.c draws it where the carrier is in the ordering table: the game's own brightness, wipes and
// fades apply to it, and (ui_text_overlay) at the window's resolution unless something is drawn over it. The original text setting, no pack for a picture, or
// gim_text 0 leave the game's drawing as it was.
//
// A pack file (text; '#' comments; pc/README.md "Pictures of text"):
//   [1]                               the image of the file (0: the first), as the tool numbers them
//   fill linear|flat [angle=N] [scope=block|line] [strength=X] [measured] | pos:rrggbb ...    how the words are coloured (a global fill: gim_text_fill)
//   line x0 y0 x1 y1 | words          the line's box in the image's pixels, its words (a leading ~: ordinary case)
// The look is settings (project_settings.cpp), RPC graphics keys and the Graphics panel's 2D / UI section: gim_text (0 off), gim_text_size (the words' width
// against the line's box), gim_text_gradient (0 the fill's average colour, 1 as it is), gim_text_caps (a small capital's height against a capital's, about 0.8;
// 0 no small capitals), gim_text_fill; the font, weight and spacing are the pictures style of ui_text (ui_text_pictures_*).
#include "draw_items.h"
#include "ui_text.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <vector>

extern "C" {
unsigned pc_vsync_count(void);

int pc_gim_text = 1;
float pc_gim_text_size = 1.0f, pc_gim_text_gradient = 1.0f, pc_gim_text_caps = 0.80f, pc_gim_text_glow = 1.0f;
char pc_gim_text_fill[1024] = ""; // the global fill: `[STEM-or-glob=]fill; ...` (see choose_fill)
int pc_gim_text_fill_force = 0; // 1: the global fill wins over the pack's
unsigned pc_gim_text_drawn; // lines drawn as carriers (RPC gim_text: tests)
}

namespace {

// The game's GIM table (BATTLE gimLbas.c): the file of each index.
const char* const STEMS[] = { "STAFF011", "STAFF001", "STAFF002", "STAFF003", "STAFF004", "STAFF005", "STAFF006", "STAFF007", "STAFF008", "GAMEOVER",
    "STAFF009", "STAFF010", "STAFF012", "ENKEI", "LOGO_G22", "STAFF013", "1142", "N_LEA", "LOGO_28", "LOGO_29", "LOGO_30", "LOGO_31", "VKP_2", "VKP_1",
    "VKP_0", "JYO", "HEADQUAT", "DEMOENK", "DADDY", "BLACK", "ENKEI2", "JO", "SUNRISE", "MAIN03", "EPI_INF", "EPILOGUE", "STAFF013", "STAFF013",
    "STAFF013", "STAFF013", "STAFF013", "STAFF013", "STAFF013", "STAFF013", "STAFF013", "STAFF013", "STAFF013", "STAFF013", "STAFF013", "STAFF013",
    "STAFF013", "EPI_E01B", "EV_MASK", "019XXX01", "DEMO_001", "DEMO_002", "DEMO_003", "DEMO_004", "DEMO_005", "DEMO_006", "DEMO_007", "DEMO_008",
    "SQUARE", "DEMOFIN" };
const int STEM_COUNT = (int)(sizeof STEMS / sizeof STEMS[0]);

struct Line {
    int box[4];                  // x0, y0, x1, y1 in the image's pixels (x1, y1 exclusive)
    std::string text;
    bool plain = false;          // set in ordinary case, not as small capitals (the words start with ~ in the pack)
};
// A fill: how the words are coloured. Data, not code (the pack's `fill` line, the setting gim_text_fill, the RPC): type flat (the first stop's colour) or linear (a
// gradient across the scope's box at `angle` degrees, 0 left to right, 90 top to bottom), scope block (all the picture's lines together) or line, any number
// of stops (position 0..1, colour) and a strength (0 the stops' average colour .. 1 as given).
struct Fill {
    bool valid = false;
    bool linear = false;
    bool per_line = false;
    float angle = 0.0f;
    float strength = 1.0f;
    std::vector<std::pair<float, unsigned>> stops;
    unsigned glow_colour = 0;
    float glow_radius = 0.0f, glow_strength = 1.0f; // the halo behind the words (glow_radius 0: none)
    std::string font;                               // the words' own font file (empty: the pictures style's), with its stretch and weight (ems)
    float font_stretch = 0.0f, font_embolden = 0.0f;
    bool font_embolden_set = false;
};
struct Pack {
    std::map<int, std::vector<Line>> images;
    std::map<int, Fill> fills;
    long long mtime = -1;
    double checked = -1e9;
};
std::map<std::string, Pack> packs;
std::vector<int> last_boxes;
std::mutex lock;

std::string pack_dir()
{
    const char* e = getenv("ROOD_GIM_TEXT");
    return e && *e ? e : "pc/user/gim_text";
}

// `type [angle=N] [scope=block|line] [strength=X] [glow=rrggbb [glow_radius=PS1px] [glow_strength=X]] [font=FILE [font_stretch=X] [font_embolden=EM]] [measured] | pos:rrggbb ...` (a flat fill's stops may be a
// lone rrggbb).
Fill parse_fill(const std::string& s)
{
    Fill f;
    size_t bar = s.find('|');
    std::string head = s.substr(0, bar), tail = bar == std::string::npos ? "" : s.substr(bar + 1);
    char type[16] = "";
    if (sscanf(head.c_str(), " %15s", type) != 1 || (strcmp(type, "flat") && strcmp(type, "linear")))
        return f;
    f.linear = !strcmp(type, "linear");
    size_t at = 0;
    while ((at = head.find('=', at)) != std::string::npos) {
        size_t k = head.rfind(' ', at);
        std::string key = head.substr(k == std::string::npos ? 0 : k + 1, at - (k == std::string::npos ? 0 : k + 1));
        const char* v = head.c_str() + at + 1;
        if (key == "angle")
            f.angle = (float)atof(v);
        else if (key == "strength")
            f.strength = (float)atof(v);
        else if (key == "scope")
            f.per_line = !strncmp(v, "line", 4);
        else if (key == "glow")
            f.glow_colour = (unsigned)strtoul(v, nullptr, 16) & 0xffffff, f.glow_radius = f.glow_radius > 0.0f ? f.glow_radius : 4.0f;
        else if (key == "glow_radius")
            f.glow_radius = std::fmax((float)atof(v), 0.0f);
        else if (key == "glow_strength")
            f.glow_strength = std::fmax((float)atof(v), 0.0f);
        else if (key == "font") {
            const char* e = v;
            while (*e && *e != ' ')
                ++e;
            f.font.assign(v, (size_t)(e - v));
        } else if (key == "font_stretch")
            f.font_stretch = std::fmax((float)atof(v), 0.0f);
        else if (key == "font_embolden")
            f.font_embolden = (float)atof(v), f.font_embolden_set = true;
        ++at;
    }
    const char* c = tail.c_str();
    while (*c) {
        while (*c == ' ')
            ++c;
        if (!*c)
            break;
        float pos = 0.0f;
        char* end;
        const char* colon = strchr(c, ':');
        const char* space = strchr(c, ' ');
        if (colon && (!space || colon < space))
            pos = (float)strtod(c, &end), c = colon + 1;
        unsigned col = (unsigned)strtoul(c, &end, 16);
        if (end == c)
            break;
        f.stops.push_back({ pos, col });
        c = end;
    }
    if (f.stops.empty())
        return f;
    std::stable_sort(f.stops.begin(), f.stops.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    f.valid = true;
    return f;
}

unsigned fill_at(const Fill& f, float t)
{
    if (f.stops.size() == 1 || !f.linear)
        return f.stops[0].second;
    t = std::fmin(std::fmax(t, 0.0f), 1.0f);
    size_t n = 1;
    while (n + 1 < f.stops.size() && f.stops[n].first < t)
        ++n;
    const auto &a = f.stops[n - 1], &b = f.stops[n];
    float u = b.first > a.first ? std::fmin(std::fmax((t - a.first) / (b.first - a.first), 0.0f), 1.0f) : (t >= b.first ? 1.0f : 0.0f);
    unsigned out = 0;
    for (unsigned sh = 0; sh < 24; sh += 8)
        out |= ((unsigned)((float)(a.second >> sh & 255) * (1.0f - u) + (float)(b.second >> sh & 255) * u + 0.5f) & 255) << sh;
    return out;
}

unsigned fill_average(const Fill& f)
{
    unsigned long sum[3] = { 0, 0, 0 };
    const int n = 32;
    for (int i = 0; i < n; i++) {
        unsigned c = fill_at(f, (i + 0.5f) / n);
        for (int k = 0; k < 3; k++)
            sum[k] += c >> (16 - 8 * k) & 255;
    }
    return (unsigned)(sum[0] / n) << 16 | (unsigned)(sum[1] / n) << 8 | (unsigned)(sum[2] / n);
}

bool glob_match(const char* pat, const char* s) // `*` only
{
    if (!*pat)
        return !*s;
    if (*pat == '*')
        return glob_match(pat + 1, s) || (*s && glob_match(pat, s + 1));
    return *pat == *s && glob_match(pat + 1, s + 1);
}

// The fill to use for a picture: the global setting's entry for its file stem (gim_text_fill: `STEM-or-glob=fill; STEM-or-glob=fill; fill` (no `=` before
// the bar: for all)) when gim_text_fill_force is on or the pack has none, else the pack's.
Fill choose_fill(const char* stem, const Fill* packs_fill)
{
    Fill global;
    static std::string last_text;
    static std::vector<std::pair<std::string, Fill>> entries;
    if (last_text != pc_gim_text_fill) {
        last_text = pc_gim_text_fill;
        entries.clear();
        size_t at = 0;
        while (at <= last_text.size()) {
            size_t end = last_text.find(';', at);
            std::string part = last_text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            size_t eq = part.find('=');
            std::string pat = "*";
            size_t sp = part.find_first_of(" |");
            if (eq != std::string::npos && eq < sp) {
                pat = part.substr(0, eq);
                part = part.substr(eq + 1);
            }
            Fill f = parse_fill(part);
            if (f.valid)
                entries.push_back({ pat, f });
            if (end == std::string::npos)
                break;
            at = end + 1;
        }
    }
    for (auto& e : entries)
        if (glob_match(e.first.c_str(), stem))
            global = e.second;
    if (global.valid && (pc_gim_text_fill_force || !packs_fill))
        return global;
    if (packs_fill && packs_fill->valid)
        return *packs_fill;
    return global;
}

void parse(Pack& p, const std::string& path)
{
    p.images.clear();
    p.fills.clear();
    FILE* f = fopen(path.c_str(), "r");
    if (!f)
        return;
    char buf[4096];
    int image = 0;
    while (fgets(buf, sizeof buf, f)) {
        std::string s = buf;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
            s.pop_back();
        if (s.empty() || s[0] == '#')
            continue;
        int n;
        if (sscanf(s.c_str(), "[%d]", &n) == 1) {
            image = n;
            continue;
        }
        if (s.compare(0, 5, "fill ") == 0) {
            Fill f = parse_fill(s.substr(5));
            if (f.valid)
                p.fills[image] = f;
            continue;
        }
        if (s.compare(0, 5, "line ") != 0)
            continue;
        Line l = {};
        size_t bar1 = s.find('|');
        if (bar1 == std::string::npos || sscanf(s.c_str() + 5, "%d %d %d %d", &l.box[0], &l.box[1], &l.box[2], &l.box[3]) != 4)
            continue;
        size_t start = s.rfind('|') + 1; // (the words follow the last bar)
        while (start < s.size() && s[start] == ' ')
            ++start;
        l.text = s.substr(start);
        if (!l.text.empty() && l.text[0] == '~') {
            l.plain = true;
            l.text.erase(0, 1);
        }
        if (!l.text.empty() && l.box[2] > l.box[0] && l.box[3] > l.box[1])
            p.images[image].push_back(l);
    }
    fclose(f);
}

// The pack of a file, re-read when it changes (polled once a second).
const Pack* pack_of(const char* stem)
{
    double now = (double)pc_vsync_count() / 60.0;
    auto it = packs.find(stem);
    if (it != packs.end() && now - it->second.checked < 1.0 && now >= it->second.checked)
        return &it->second;
    Pack& p = packs[stem];
    p.checked = now;
    std::string path = pack_dir() + "/" + stem + ".txt";
    struct stat st;
    long long m = stat(path.c_str(), &st) == 0 ? (long long)st.st_mtime * 4096LL + (long long)(st.st_size & 4095) : -2;
    if (m != p.mtime) {
        p.mtime = m;
        if (m >= 0)
            parse(p, path);
        else
            p.images.clear();
    }
    return &p;
}

bool enabled() { return pc_gim_text && pc_ui_text == PC_UI_TEXT_FONT; }

struct Mapping {
    float left0, top0, k;
};
Mapping mapping(int columns, int rows, int scale, int ox, int oy)
{
    return { (float)(((-columns * scale) >> 7) + ox + 160), (float)((((-rows) * scale * 15) >> 13) + oy + 120), (float)scale / 4096.0f };
}

// A pack line as a line of the picture's carrier: its box in the screen's PS1 pixels (the picture's own mapping), the fill and the settings.
void make_line(const Line& l, const Mapping& m, const Fill& fill, const float block[4], pc_ui_picture_line* out)
{
    memset(out, 0, sizeof *out);
    snprintf(out->text, sizeof out->text, "%s", l.text.c_str());
    out->plain = l.plain;
    out->caps = pc_gim_text_caps;
    out->size = pc_gim_text_size;
    out->box[0] = m.left0 + (float)l.box[0] * m.k, out->box[1] = m.top0 + (float)l.box[1] * m.k;
    out->box[2] = m.left0 + (float)l.box[2] * m.k, out->box[3] = m.top0 + (float)l.box[3] * m.k;
    out->linear = fill.linear, out->per_line = fill.per_line;
    out->angle = fill.angle;
    out->strength = std::fmin(std::fmax(fill.strength, 0.0f), 1.0f) * std::fmin(std::fmax(pc_gim_text_gradient, 0.0f), 1.0f);
    out->nstops = (int)std::min<size_t>(fill.stops.size(), 8);
    for (int i = 0; i < out->nstops; i++)
        out->pos[i] = fill.stops[(size_t)i].first, out->colour[i] = fill.stops[(size_t)i].second;
    memcpy(out->block, block, sizeof out->block);
    out->glow_colour = fill.glow_colour;
    out->glow_radius = fill.glow_radius * m.k;
    out->glow_strength = fill.glow_strength * std::fmax(pc_gim_text_glow, 0.0f);
    snprintf(out->font, sizeof out->font, "%s", fill.font.c_str());
    out->stretch = fill.font_stretch, out->embolden = fill.font_embolden, out->embolden_set = fill.font_embolden_set;
}

// One carrier per piece of a line (a wide one is set as pieces: an image is at most 255 texels across) and, in a scanline wipe (`left`, `right`), per slice of equal
// visible span: `place(xy, part, col0, col1, row0, row1)` makes the carrier item at `xy` (the image's top-left) and registers `part` with it.
template <class Place>
void emit_job(const pc_ui_picture_line& job, const short* left, const short* right, Place place)
{
    float fx, fy;
    int fw, fh;
    if (!pc_ui_text_picture_extent(&job, &fx, &fy, &fw, &fh))
        return;
    int pieces = (fw + 239) / 240;
    for (int piece = 0; piece < pieces; piece++) {
        pc_ui_picture_line part = job;
        if (pieces > 1)
            part.clip[0] = fx + (float)(fw * piece / pieces), part.clip[1] = fx + (float)(fw * (piece + 1) / pieces);
        float ix, iy;
        int iw, ih;
        if (!pc_ui_text_picture_extent(&part, &ix, &iy, &iw, &ih))
            continue;
        int xy = ((int)ix & 0xffff) | (int)iy << 16;
        if (!left) {
            place(xy, part, 0, 0, 0, 0);
            continue;
        }
        int row = 0;
        while (row < ih) {
            int y = (int)iy + row;
            auto span = [&](int yy, int* c0, int* c1) {
                *c0 = 0, *c1 = 0;
                if (yy < 0 || yy >= 240)
                    return;
                *c0 = std::max(0, (int)left[yy] - (int)ix), *c1 = std::min(iw, (int)right[yy] - (int)ix);
            };
            int c0, c1, d0, d1, end = row + 1;
            span(y, &c0, &c1);
            for (; end < ih; end++) {
                span((int)iy + end, &d0, &d1);
                if (d0 != c0 || d1 != c1)
                    break;
            }
            if (c1 > c0)
                place(xy, part, c0, c1, row, end);
            row = end;
        }
    }
}

// The halos of all the lines and the words of all of them, as `emit` takes each, in the order they are made: the halos first when `halos_first` (what is made first is
// drawn first: items drawn at once), else the words first (an ordering table draws what was put in a place last first: the halos below the words).
template <class Emit>
void emit_lines(const std::vector<Line>& lines, const Mapping& m, const Fill& fill, bool halos_first, Emit emit)
{
    float block[4] = { 1e9f, 1e9f, -1e9f, -1e9f };
    for (const Line& l : lines) {
        block[0] = std::fmin(block[0], m.left0 + (float)l.box[0] * m.k), block[1] = std::fmin(block[1], m.top0 + (float)l.box[1] * m.k);
        block[2] = std::fmax(block[2], m.left0 + (float)l.box[2] * m.k), block[3] = std::fmax(block[3], m.top0 + (float)l.box[3] * m.k);
    }
    std::vector<pc_ui_picture_line> jobs;
    for (const Line& l : lines) {
        pc_ui_picture_line job;
        make_line(l, m, fill, block, &job);
        jobs.push_back(job);
    }
    for (int pass = 0; pass < 2; pass++)
        for (pc_ui_picture_line job : jobs) {
            bool halo = pass == (halos_first ? 0 : 1);
            if (halo && !(job.glow_radius > 0.0f && job.glow_strength > 0.0f))
                continue;
            job.glow_pass = halo;
            emit(job);
            pc_gim_text_drawn += !halo;
        }
}

} // namespace

extern "C" int vs_battle_spriteItem(unsigned* ot, int brightness, int xy, int wh, int mode, int uv_clut);
extern "C" const void* vs_battle_spriteCommand(int item);

// vs_render_drawGimTiles and the scanline wipe (gim_text_bridge.c): does this picture have words in the player's pack (and the text setting is the fonts)? Then its
// boxes are remembered for the drawer to leave out (pc_gim_text_boxes) and 1 is returned.
extern "C" int pc_gim_text_query(int id, int layer)
{
    if (!enabled() || id < 0 || id >= STEM_COUNT)
        return 0;
    std::lock_guard<std::mutex> hold(lock);
    const Pack* p = pack_of(STEMS[id]);
    auto it = p->images.find(layer);
    if (it == p->images.end())
        return 0;
    last_boxes.clear();
    for (const Line& l : it->second)
        for (int k = 0; k < 4; k++)
            last_boxes.push_back(l.box[k]);
    return 1;
}

// The boxes (x0, y0, x1, y1 in the picture's pixels) of the words just asked for: the drawer leaves what they cover out of the picture. Returns the box count.
extern "C" int pc_gim_text_boxes(int* out)
{
    std::lock_guard<std::mutex> hold(lock);
    int n = std::min((int)last_boxes.size() / 4, 16);
    for (int i = 0; i < n * 4; i++)
        out[i] = last_boxes[(size_t)i];
    return n;
}

// The carriers of the picture's lines, at the place in the ordering table `ot` where the picture is drawn: a 1 x 1 sprite item each (its brightness `brightness`:
// the picture's, bit 8 semi-transparent), registered with ui_text.c, which draws the line's glyph image there (gpu_hw.c picture_rect). With `spans` (a scanline wipe:
// 240 lines of left and right in the picture's 320-wide screen) each line is cut in slices of equal visible span, one carrier each.
extern "C" void pc_gim_text_carry(int id, int layer, int columns, int rows, int scale, int ox, int oy, int brightness, unsigned* ot, const short* left, const short* right)
{
    if (!enabled() || id < 0 || id >= STEM_COUNT)
        return;
    std::vector<Line> lines;
    Fill fill;
    {
        std::lock_guard<std::mutex> hold(lock);
        const Pack* p = pack_of(STEMS[id]);
        auto it = p->images.find(layer);
        if (it == p->images.end())
            return;
        lines = it->second;
        auto fi = p->fills.find(layer);
        fill = choose_fill(STEMS[id], fi == p->fills.end() ? nullptr : &fi->second);
    }
    if (!fill.valid)
        fill.stops = { { 0.0f, 0xFFFFFF } }, fill.valid = true;
    Mapping m = left ? Mapping{ 0.0f, 0.0f, 1.0f } : mapping(columns, rows, scale, ox, oy); // (the wipe draws the whole 320 x 240 picture at the origin)
    int uvclut = PC_PICTURE_U | PC_PICTURE_V << 8 | PC_PICTURE_CLUT << 16;
    emit_lines(lines, m, fill, false, [&](const pc_ui_picture_line& job) {
        emit_job(job, left, right, [&](int xy, const pc_ui_picture_line& part, int c0, int c1, int r0, int r1) {
            int item = vs_battle_spriteItem(ot, brightness, xy, 0x10001, 0, uvclut);
            pc_ui_text_picture_add(vs_battle_spriteCommand(item), &part, c0, c1, r0, r1);
        });
    });
}

// The loading screen: "NOW LOADING" is a picture in the executable (126 x 20 texels at the centre of the 320 x 224 screen), uploaded into the frame
// buffers with LoadImage; no ordering table, no packet. With the pack NOWLOADING (pc/tools/gim_text_pack.py: measured from the executable's image) and the modern
// fonts on, _displayLoadingScreen leaves the upload out and draws the pack's lines as carriers drawn at once (DrawPrim), as the picture's own pixels would be
// shown: the picture's mapping is its place on the screen, and the 4:3 and 16:9 views differ only in the view's transform, as for every picture.
namespace {
bool loading_lines(std::vector<Line>* lines, Fill* fill)
{
    if (!enabled())
        return false;
    std::lock_guard<std::mutex> hold(lock);
    const Pack* p = pack_of("NOWLOADING");
    auto it = p->images.find(0);
    if (it == p->images.end())
        return false;
    if (lines)
        *lines = it->second;
    if (fill) {
        auto fi = p->fills.find(0);
        *fill = choose_fill("NOWLOADING", fi == p->fills.end() ? nullptr : &fi->second);
        if (!fill->valid)
            fill->stops = { { 0.0f, 0xFFFFFF } }, fill->valid = true;
    }
    return true;
}
} // namespace

// 1: the loading screen's words are drawn as text (the upload of the picture is left out).
extern "C" int pc_gim_text_loading(void) { return loading_lines(nullptr, nullptr); }

// Draws the loading screen's words (image `w` x `h` texels centred in 320 x 224) into the frame buffer the draw environment names: every carrier a sprite item of
// one packet drawn at once (pc_frame_immediate), each registered with the command word of its own primitive in it.
extern "C" void pc_gim_text_loading_draw(int w, int h)
{
    std::vector<Line> lines;
    Fill fill;
    if (!loading_lines(&lines, &fill))
        return;
    Mapping m = { (float)((320 - w) / 2), (float)((224 - h) / 2), 1.0f };
    int uvclut = PC_PICTURE_U | PC_PICTURE_V << 8 | PC_PICTURE_CLUT << 16, first = pc_frame_count();
    std::vector<pc_ui_picture_line> parts;
    emit_lines(lines, m, fill, true, [&](const pc_ui_picture_line& job) {
        emit_job(job, nullptr, nullptr, [&](int xy, const pc_ui_picture_line& part, int, int, int, int) {
            pc_frame_sprite(0, (short)xy, (short)(xy >> 16), 1, 1, uvclut & 255, uvclut >> 8 & 255, 0x808080, uvclut >> 16 & 0xffff, PC_ITEM_INHERIT, 0);
            parts.push_back(part);
        });
    });
    std::vector<const void*> commands(parts.size(), nullptr);
    pc_frame_immediate_commands(first, commands.data(), (int)commands.size());
    for (size_t i = 0; i < parts.size(); i++)
        if (commands[i])
            pc_ui_text_picture_add(commands[i], &parts[i], 0, 0, 0, 0);
    pc_frame_immediate(first);
}
