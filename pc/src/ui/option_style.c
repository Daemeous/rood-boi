/* The Option screens' look and feel as data (option_style.h). */
#include "option_style.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char* name;
    double def, min, max;
    const char* help;
} Field;

/* The defaults are rood.options' values (pc/plugins/rood.options/main.lua M.style). */
static const Field fields[PC_STYLE_COUNT] = {
    { "classic_row_x", 65, 0, 200, "A page row's window left edge (PS1 pixels); it reaches the screen's right edge" },
    { "classic_row_w", 255, 64, 255, "A page row's window width (the game's windows are at most 255)" },
    { "classic_value_x", 140, 40, 250, "The value column, from the row window's left edge" },
    { "classic_uppercase", 1, 0, 1, "Labels and values in capitals, as the game's own rows" },
    { "classic_toggle_gap", 10, 0, 40, "Between ON and OFF" },
    { "classic_bar_colour", 1, 0, 3, "The slider gauge's colours (the HP, MP and RISK bars': 0-3)" },
    { "classic_bar_width", 64, 8, 64, "The gauge's length at its maximum (the game's bars: 64)" },
    { "classic_lit", 1, 0, 3, "The current value's text colour (0 white, 1 the game's lit yellow, 3 grey)" },
    { "classic_other", 3, 0, 3, "The other toggle value's text colour" },
    { "classic_help_width", 270, 100, 300, "A help line's width in the information box (the game's own descriptions: 270 at the longest)" },
    { "classic_help_seconds", 3, 1, 10, "A help longer than the information box holds is shown a box at a time, this long each" },
    { "classic_help_lines", 2, 1, 2, "The information box's lines (the game's own descriptions: up to 2)" },
    { "confirm_seconds", 10, 3, 60, "\"Keep these settings?\" reverts by itself after this long" },
};
static double values[PC_STYLE_COUNT];
static int ready;
static unsigned generation = 1;

static void start(void)
{
    if (ready)
        return;
    ready = 1;
    for (int i = 0; i < PC_STYLE_COUNT; i++)
        values[i] = fields[i].def;
}

int pc_option_style_count(void) { return PC_STYLE_COUNT; }
const char* pc_option_style_name(int i) { return i >= 0 && i < PC_STYLE_COUNT ? fields[i].name : NULL; }
const char* pc_option_style_help(int i) { return i >= 0 && i < PC_STYLE_COUNT ? fields[i].help : NULL; }
int pc_option_style_index(const char* name)
{
    for (int i = 0; name && i < PC_STYLE_COUNT; i++)
        if (!strcmp(fields[i].name, name))
            return i;
    return -1;
}
double pc_option_style_value(int i)
{
    start();
    return i >= 0 && i < PC_STYLE_COUNT ? values[i] : 0;
}
double pc_option_style_default(int i) { return i >= 0 && i < PC_STYLE_COUNT ? fields[i].def : 0; }
int pc_option_style_int(int i) { return (int)lround(pc_option_style_value(i)); }
int pc_option_style_set(const char* name, double value)
{
    int i = pc_option_style_index(name);
    if (i < 0 || value != value)
        return 0;
    start();
    value = value < fields[i].min ? fields[i].min : value > fields[i].max ? fields[i].max : value;
    if (values[i] != value) {
        values[i] = value;
        ++generation;
    }
    return 1;
}
void pc_option_style_reset(void)
{
    start();
    for (int i = 0; i < PC_STYLE_COUNT; i++)
        values[i] = fields[i].def;
    ++generation;
}
unsigned pc_option_style_generation(void) { return generation; }
int pc_option_style_json(char* out, int size)
{
    int n = 0;
    if (!out || size <= 0)
        return 0;
    start();
    n += snprintf(out + n, (size_t)(size - n), "{");
    for (int i = 0; i < PC_STYLE_COUNT && n < size; i++)
        n += snprintf(out + n, (size_t)(size - n), "%s\"%s\":%g", i ? "," : "", fields[i].name, values[i]);
    if (n < size)
        n += snprintf(out + n, (size_t)(size - n), "}");
    return n < size ? n : size - 1;
}
