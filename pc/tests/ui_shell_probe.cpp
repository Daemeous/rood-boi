// The modern menu shell's navigation (pc/src/ui/ui_shell.cpp) without the game.
#include "ui/ui_shell.h"
#include "ui/ui_theme.h"
#include <cstdio>
#include <cstring>
#include <string>

int pc_ui_menus = 1;
static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static int tick(unsigned buttons)
{
    pc_ui_input in = {buttons, buttons};
    return pc_ui_shell_tick(&in);
}
static bool json_has(const char* needle)
{
    static char buf[16384];
    pc_ui_shell_json(buf, sizeof buf);
    return std::strstr(buf, needle) != nullptr;
}

int main()
{
    pc_ui_commands all = {0x3FF, 0, 0};
    pc_ui_shell_open(&all, 0);
    CHECK(pc_ui_shell_visible());
    CHECK(json_has("\"tab\":\"ashley\""));
    CHECK(tick(0) == 0);
    // Ashley: Status is command 4 -> result 5
    CHECK(tick(PC_UI_CONFIRM) == 5);
    // the game shows Status; when it returns, the shell is opened again (remember = 1) on the same tab
    pc_ui_shell_open(&all, 1);
    CHECK(json_has("\"tab\":\"ashley\""));
    // R1 -> Equipment, L1 back, L1 wraps to System
    tick(PC_UI_TAB_NEXT);
    CHECK(json_has("\"tab\":\"equipment\""));
    tick(PC_UI_TAB_PREV);
    tick(PC_UI_TAB_PREV);
    CHECK(json_has("\"tab\":\"system\""));
    // System rows: Data, Options, Score, Quick manual, Menus; Down x1 = Options (7) -> 8
    tick(PC_UI_DOWN);
    CHECK(tick(PC_UI_CONFIRM) == 8);
    pc_ui_shell_open(&all, 1);
    // the Menus row switches the setting, the shell stays
    tick(PC_UI_DOWN); tick(PC_UI_DOWN); tick(PC_UI_DOWN);
    CHECK(json_has("\"row\":\"menus\""));
    CHECK(tick(PC_UI_CONFIRM) == 0);
    CHECK(pc_ui_menus == 0);
    tick(PC_UI_CONFIRM);
    CHECK(pc_ui_menus == 1);
    // not remembered: back to Ashley
    pc_ui_shell_open(&all, 0);
    CHECK(json_has("\"tab\":\"ashley\""));
    // Magick unselectable (MP 0: mask bit 0x20): greyed with a reason, and confirm still opens
    // the game's Magick (command 0 -> 1), as the original top level does
    pc_ui_commands no_magic = {0x3FF, 1, 0x20};
    pc_ui_shell_open(&no_magic, 0);
    for (int i = 0; i < 4; ++i) tick(PC_UI_TAB_NEXT); // Ashley Equipment Items Abilities Magick
    CHECK(json_has("\"tab\":\"magick\""));
    CHECK(json_has("\"enabled\":false"));
    CHECK(json_has("\"reason\":\"Not enough MP.\""));
    CHECK(tick(PC_UI_CONFIRM) == 1);
    CHECK(!pc_ui_shell_visible());
    // the reason follows the game's order (lowest bit the command cares about): hanging (4) before MP (0x20)
    pc_ui_commands hanging = {0x3FF, 1, 0x24};
    pc_ui_shell_open(&hanging, 1);
    CHECK(json_has("\"reason\":\"Not while hanging from a ledge.\""));
    // a bit Magick does not care about (8, battle mode) gives the general reason
    pc_ui_commands other = {0x3FF, 1, 0x08};
    pc_ui_shell_open(&other, 1);
    CHECK(json_has("\"reason\":\"Not usable right now"));
    // selectable rows carry no reason
    pc_ui_shell_open(&all, 1);
    CHECK(!json_has("\"reason\""));
    // a command the game has not unlocked (menuFlags bit clear) hides its row: Abilities without Break arts
    pc_ui_commands no_arts = {0x3FF & ~2u, 0, 0};
    pc_ui_shell_open(&no_arts, 0);
    for (int i = 0; i < 3; ++i) tick(PC_UI_TAB_NEXT);
    CHECK(json_has("\"tab\":\"abilities\""));
    CHECK(!json_has("break_arts"));
    // Back and Exit close
    CHECK(tick(PC_UI_BACK) == -1);
    CHECK(!pc_ui_shell_visible());
    pc_ui_shell_open(&all, 0);
    CHECK(tick(PC_UI_EXIT) == -1);
    // pc_ui_shell_close hides without a result (another menu session: loot, shortcuts...)
    pc_ui_shell_open(&all, 0);
    pc_ui_shell_close();
    CHECK(!pc_ui_shell_visible());
    CHECK(tick(0) == -1);
    // tabs without rows are skipped: no Magick (bit 0), no Abilities (bits 1, 2)
    {
        pc_ui_commands early = {0x3FF & ~7u, 0, 0};
        pc_ui_shell_open(&early, 0);
        tick(PC_UI_TAB_NEXT); tick(PC_UI_TAB_NEXT); // Equipment, Items
        CHECK(json_has("\"tab\":\"items\""));
        tick(PC_UI_TAB_NEXT); // Abilities and Magick are empty
        CHECK(json_has("\"tab\":\"lea_monde\""));
        tick(PC_UI_TAB_PREV);
        CHECK(json_has("\"tab\":\"items\""));
        // reopened (cursor memory) on a tab that is empty now: the next tab with rows
        pc_ui_shell_open(&all, 0);
        for (int i = 0; i < 4; ++i) tick(PC_UI_TAB_NEXT);
        CHECK(json_has("\"tab\":\"magick\""));
        pc_ui_shell_open(&early, 1);
        CHECK(json_has("\"tab\":\"lea_monde\""));
    }
    {
        ui::Theme t;
        CHECK(t.accent == 0xFF3C36B5u); // default crimson #b5363c as ImU32 (ABGR)
        std::string err;
        CHECK(ui::theme_parse("{\"colors\":{\"accent\":\"#102030\"},\"sizes\":{\"tab\":17}}", t, &err));
        CHECK(t.accent == 0xFF302010u);
        CHECK(t.tab_size == 17.0f);
        CHECK(!ui::theme_parse("{\"colors\":{\"accent\":\"red\"}}", t, &err));
        CHECK(!err.empty());
        // colours: every digit hex (strtoul alone takes a sign or spaces)
        CHECK(!ui::theme_parse("{\"colors\":{\"accent\":\"#12345g\"}}", t, &err));
        CHECK(!ui::theme_parse("{\"colors\":{\"accent\":\"#+12345\"}}", t, &err));
        CHECK(!ui::theme_parse("{\"colors\":{\"accent\":\"# 12345\"}}", t, &err));
        CHECK(!ui::theme_parse("{\"colors\":{\"accent\":\"#-1020304\"}}", t, &err));
        CHECK(ui::theme_parse("{\"colors\":{\"accent\":\"#A0b0C0ff\"}}", t, &err));
        CHECK(t.accent == 0xFFC0B0A0u);
        // numbers: non-finite rejected, out of range clamped
        CHECK(!ui::theme_parse("{\"sizes\":{\"tab\":inf}}", t, &err));
        CHECK(!ui::theme_parse("{\"sizes\":{\"row_height\":nan}}", t, &err));
        CHECK(!ui::theme_parse("{\"motion\":{\"tab_slide\":1e999}}", t, &err));
        CHECK(!ui::theme_parse("{\"sizes\":{\"tab\":\"big\"}}", t, &err));
        CHECK(ui::theme_parse("{\"sizes\":{\"tab\":500,\"body\":-3,\"row_height\":2,\"margin_x\":-10,"
                              "\"rail_width\":99999},\"motion\":{\"tab_slide\":60,\"focus_grow\":-1}}", t, &err));
        CHECK(t.tab_size == 96.0f && t.body_size == 4.0f && t.row_height == 16.0f && t.margin_x == 0.0f);
        CHECK(t.rail_width == 2000.0f && t.tab_slide == 5.0f && t.focus_grow == 0.0f);
        // fonts: a path string or an error
        CHECK(!ui::theme_parse("{\"fonts\":{\"name\":5}}", t, &err));
        CHECK(err.find("fonts.name") != std::string::npos);
        CHECK(ui::theme_parse("{\"fonts\":{\"name\":\"x.ttf\"}}", t, &err) && t.font_name == "x.ttf");
    }
    std::printf(failures ? "ui_shell_probe: %d failures\n" : "ui_shell_probe: ok\n", failures);
    return failures != 0;
}
