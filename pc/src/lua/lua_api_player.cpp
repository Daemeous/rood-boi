// rood.game's player state (pc/docs/Extensions.md "Player state"): the bag, the
// weapons, shields, armour and accessories, what is equipped, the workshop, and the
// game's own combine run on copies. Read only.
//   rood.game.inventory()    nil, or {counts, items, keys, sigils, blades, grips, gems}
//   rood.game.weapons()      {weapon, ...} (each with its blade, grip and gems)
//   rood.game.shields()      {shield, ...} (each with its gems)
//   rood.game.armour()       {armor, ...} (body armour; accessories apart)
//   rood.game.accessories()  {accessory, ...}
//   rood.game.equipment()    {weapon, shield, right_arm, left_arm, head, body, legs, accessory}
//   rood.game.workshop()     nil, or {number, name, materials}
//   rood.game.labels()       the game's words: {classes, affinities, types}
//   rood.game.combine(a, b)  the result the game would produce, or nil and why
//   rood.game.craftable()    every combination the workshop allows, with results
// The same structures go to the RPC (game_state, combine_preview: pc_player_rpc).
// player_bridge.c fills the plain records (player_query.h); each query's value is
// built at most once per game vsync, and each plugin gets one read-only table per
// query per vsync (the same table until the next vsync, as rood.game.ashley's).
#include "rl_internal.h"
#include "hud/player_query.h"
#include "light_prefabs.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace rl {
namespace {
unsigned long long ticks = 1;

const char* const class_keys[PC_PQ_CLASSES] = { "human", "beast", "undead", "phantom", "dragon", "evil" };
const char* const affinity_keys[PC_PQ_AFFINITIES] = { "physical", "air", "fire", "earth", "water", "light", "dark" };
const char* const type_keys[PC_PQ_TYPES] = { "blunt", "edged", "piercing" };
const char* const slot_keys[PC_PQ_SLOTS] = { "weapon", "shield", "right_arm", "left_arm", "head", "body", "legs",
    "accessory" };
const char* const kind_names[PC_PQ_KINDS] = { "weapon", "blade", "grip", "shield", "armor", "accessory", "gem", "item" };
const char* const misc_names[3] = { "item", "key", "sigil" };

Value I(long long v) { return Value::integer(v); }
Value S(const char* s) { return Value::str(s ? s : ""); }
Value B(bool b) { return Value::boolean(b); }

Value keyed(const int* values, const char* const* keys, int n)
{
    Value t = Value::new_table();
    for (int i = 0; i < n; ++i)
        t.set(keys[i], I(values[i]));
    return t;
}

const char* kind_name(const pc_pq_part& p)
{
    if (p.kind == PC_PQ_MISC)
        return misc_names[p.misc_kind >= 0 && p.misc_kind < 3 ? p.misc_kind : 0];
    return p.kind >= 0 && p.kind < PC_PQ_KINDS ? kind_names[p.kind] : "?";
}

// One record: the fields its kind has (Extensions.md "Player state" lists them).
Value part(const pc_pq_part& p)
{
    Value t = Value::new_table();
    int k = p.kind;
    bool equipment = k != PC_PQ_MISC;
    t.set("kind", S(kind_name(p)));
    t.set("record", I(p.record));
    t.set("id", I(p.id));
    t.set("name", S(p.name));
    if (k == PC_PQ_MISC) {
        t.set("count", I(p.count));
        return t;
    }
    if (p.material[0])
        t.set("material", S(p.material));
    if (p.category[0])
        t.set("category", S(p.category));
    if (k == PC_PQ_WEAPON || k == PC_PQ_BLADE || k == PC_PQ_SHIELD || k == PC_PQ_ARMOR) {
        t.set("dp", I(p.dp));
        t.set("dp_max", I(p.dp_max));
    }
    if (k == PC_PQ_WEAPON || k == PC_PQ_BLADE || k == PC_PQ_SHIELD) {
        t.set("pp", I(p.pp));
        t.set("pp_max", I(p.pp_max));
    }
    if (equipment) {
        t.set("str", I(p.str));
        t.set("int", I(p.intel));
        t.set("agl", I(p.agl));
    }
    if (k != PC_PQ_GRIP) {
        t.set("classes", keyed(p.classes, class_keys, PC_PQ_CLASSES));
        t.set("affinities", keyed(p.affinities, affinity_keys, PC_PQ_AFFINITIES));
    }
    if (k == PC_PQ_WEAPON || k == PC_PQ_GRIP || k == PC_PQ_SHIELD || k == PC_PQ_ARMOR || k == PC_PQ_ACCESSORY)
        t.set("types", keyed(p.types, type_keys, PC_PQ_TYPES));
    if (k == PC_PQ_WEAPON || k == PC_PQ_BLADE) {
        if (p.damage_type >= 0 && p.damage_type < PC_PQ_TYPES)
            t.set("damage_type", S(type_keys[p.damage_type]));
        t.set("range", I(p.range));
        t.set("risk", I(p.risk));
        t.set("two_handed", B(p.two_handed != 0));
    }
    if (k == PC_PQ_WEAPON || k == PC_PQ_GRIP || k == PC_PQ_SHIELD)
        t.set("gem_slots", I(p.gem_slots));
    if (p.weapon)
        t.set("weapon", I(p.weapon));
    if (p.shield)
        t.set("shield", I(p.shield));
    t.set("equipped", B(p.equipped != 0));
    t.set("where", S(p.equipped ? "equipped" : "bag"));
    if (p.slot >= 0 && p.slot < PC_PQ_SLOTS)
        t.set("slot", S(slot_keys[p.slot]));
    if (k == PC_PQ_WEAPON && p.quick)
        t.set("quick", I(p.quick));
    return t;
}

Value set_value(const pc_pq_set& s, bool weapon)
{
    Value t = part(s.whole);
    if (weapon) {
        t.set("blade", part(s.blade));
        if (s.grip.record)
            t.set("grip", part(s.grip));
    }
    Value gems = Value::new_table();
    for (int i = 0; i < s.gem_count; ++i)
        gems.push_back(part(s.gems[i]));
    t.set("gems", gems);
    return t;
}

Value counted(int count, int capacity)
{
    Value t = Value::new_table();
    t.set("count", I(count));
    t.set("capacity", I(capacity));
    return t;
}

const pc_pq_bag* bag_now() { return pc_player_bag ? pc_player_bag() : nullptr; }

Value inventory_value()
{
    const pc_pq_bag* b = bag_now();
    if (!b)
        return Value();
    static const char* const names[PC_PQ_BAG_CATEGORIES] = { "weapons", "blades", "grips", "shields", "armor", "gems",
        "misc" };
    Value t = Value::new_table();
    Value counts = Value::new_table();
    for (int c = 0; c < PC_PQ_BAG_CATEGORIES; ++c)
        counts.set(names[c], counted(b->count[c], b->capacity[c]));
    t.set("counts", counts);
    Value lists[3] = { Value::new_table(), Value::new_table(), Value::new_table() };
    for (int i = 0; i < b->misc_count; ++i) {
        int m = b->misc[i].misc_kind;
        lists[m >= 0 && m < 3 ? m : 0].push_back(part(b->misc[i]));
    }
    t.set("items", lists[PC_PQ_ITEM]);
    t.set("keys", lists[PC_PQ_KEY]);
    t.set("sigils", lists[PC_PQ_SIGIL]);
    Value blades = Value::new_table(), grips = Value::new_table(), gems = Value::new_table();
    for (int i = 0; i < b->blade_count; ++i)
        blades.push_back(part(b->blades[i]));
    for (int i = 0; i < b->grip_count; ++i)
        grips.push_back(part(b->grips[i]));
    for (int i = 0; i < b->gem_count; ++i)
        gems.push_back(part(b->gems[i]));
    t.set("blades", blades);
    t.set("grips", grips);
    t.set("gems", gems);
    return t;
}

Value weapons_value()
{
    const pc_pq_bag* b = bag_now();
    if (!b)
        return Value();
    Value t = Value::new_table();
    for (int i = 0; i < b->weapon_count; ++i)
        t.push_back(set_value(b->weapons[i], true));
    return t;
}

Value shields_value()
{
    const pc_pq_bag* b = bag_now();
    if (!b)
        return Value();
    Value t = Value::new_table();
    for (int i = 0; i < b->shield_count; ++i)
        t.push_back(set_value(b->shields[i], false));
    return t;
}

Value armor_value(bool accessories)
{
    const pc_pq_bag* b = bag_now();
    if (!b)
        return Value();
    Value t = Value::new_table();
    for (int i = 0; i < b->armor_count; ++i)
        if ((b->armor[i].kind == PC_PQ_ACCESSORY) == accessories)
            t.push_back(part(b->armor[i]));
    return t;
}

Value equipment_value()
{
    const pc_pq_bag* b = bag_now();
    if (!b)
        return Value();
    Value t = Value::new_table();
    for (int slot = 0; slot < PC_PQ_SLOTS; ++slot) {
        int r = b->equipped[slot];
        if (!r)
            continue;
        if (slot == PC_PQ_SLOT_WEAPON) {
            for (int i = 0; i < b->weapon_count; ++i)
                if (b->weapons[i].whole.record == r)
                    t.set(slot_keys[slot], set_value(b->weapons[i], true));
        } else if (slot == PC_PQ_SLOT_SHIELD) {
            for (int i = 0; i < b->shield_count; ++i)
                if (b->shields[i].whole.record == r)
                    t.set(slot_keys[slot], set_value(b->shields[i], false));
        } else {
            for (int i = 0; i < b->armor_count; ++i)
                if (b->armor[i].record == r)
                    t.set(slot_keys[slot], part(b->armor[i]));
        }
    }
    return t;
}

Value workshop_value()
{
    pc_pq_workshop w;
    if (!pc_player_workshop || !pc_player_workshop(&w))
        return Value();
    Value t = Value::new_table();
    t.set("number", I(w.workshop));
    t.set("name", S(w.name));
    Value materials = Value::new_table();
    for (int i = 0; i < w.material_count; ++i)
        materials.push_back(S(w.material_names[i]));
    t.set("materials", materials);
    return t;
}

Value labels_value()
{
    Value t = Value::new_table();
    const char* groups[3] = { "classes", "affinities", "types" };
    const int counts[3] = { PC_PQ_CLASSES, PC_PQ_AFFINITIES, PC_PQ_TYPES };
    for (int g = 0; g < 3; ++g) {
        Value list = Value::new_table();
        for (int i = 0; i < counts[g]; ++i)
            list.push_back(S(pc_player_label ? pc_player_label(g, i) : ""));
        t.set(groups[g], list);
    }
    return t;
}

// The record's name as the bag lists it (a blade, shield or armor record), for a
// combine's first / second.
std::string record_name(int kind, int record)
{
    const pc_pq_bag* b = bag_now();
    if (!b)
        return "";
    if (kind == PC_PQ_BLADE) {
        for (int i = 0; i < b->blade_count; ++i)
            if (b->blades[i].record == record)
                return b->blades[i].name;
    } else if (kind == PC_PQ_SHIELD) {
        for (int i = 0; i < b->shield_count; ++i)
            if (b->shields[i].whole.record == record)
                return b->shields[i].whole.name;
    } else {
        for (int i = 0; i < b->armor_count; ++i)
            if (b->armor[i].record == record)
                return b->armor[i].name;
    }
    return "";
}

Value combine_value(const pc_pq_combine& c, bool raw)
{
    Value t = Value::new_table();
    t.set("kind", S(kind_names[c.kind]));
    Value first = Value::new_table(), second = Value::new_table();
    first.set("record", I(c.a));
    first.set("name", S(record_name(c.kind, c.a).c_str()));
    second.set("record", I(c.b));
    second.set("name", S(record_name(c.kind, c.b).c_str()));
    t.set("first", first);
    t.set("second", second);
    t.set("allowed", B(c.allowed != 0));
    if (!c.allowed)
        t.set("why", S(c.why));
    t.set("result", part(c.result));
    if (raw) {
        std::string hex;
        char two[3];
        for (int i = 0; i < c.raw_size; ++i) {
            std::snprintf(two, sizeof two, "%02x", c.raw[i]);
            hex += two;
        }
        t.set("raw", S(hex.c_str()));
    }
    return t;
}

Value craftable_value(bool raw)
{
    Value t = Value::new_table();
    if (!pc_player_craftable)
        return t;
    std::vector<pc_pq_combine> out(PC_PQ_COMBOS);
    int n = pc_player_craftable(out.data(), (int)out.size());
    for (int i = 0; i < n; ++i)
        t.push_back(combine_value(out[i], raw));
    return t;
}

// JSON as to_json writes it, except that the lists these queries make stay lists
// when they are empty ([] where to_json would write {}).
bool list_key(const std::string& k)
{
    static const char* const lists[] = { "items", "keys", "sigils", "blades", "grips", "gems", "materials", "classes",
        "affinities", "types", "weapons", "shields", "armour", "accessories", "craftable" };
    for (const char* l : lists)
        if (k == l)
            return true;
    return false;
}

std::string json(const Value& v, bool list)
{
    if (v.kind != Value::TABLE)
        return to_json(v);
    if (v.table.empty())
        return list ? "[]" : "{}";
    if (v.length() == v.table.size()) {
        std::string s = "[";
        for (size_t i = 0; i < v.table.size(); ++i)
            s += (i ? "," : "") + json(v.table[i].second, false);
        return s + "]";
    }
    std::string s = "{";
    bool first = true;
    for (const auto& kv : v.table) {
        if (kv.second.kind == Value::NIL || kv.first.kind != Value::STRING)
            continue;
        s += (first ? "" : ",") + prefab::json_quote(kv.first.s) + ":"
            + json(kv.second, list_key(kv.first.s) && kv.second.kind == Value::TABLE && kv.second.length() == kv.second.table.size());
        first = false;
    }
    return s + "}";
}

// The kind and record of something to combine: a table from these queries (a
// blade, shield or armor; a weapon stands for its blade) or {kind =, record =}.
bool combinable(const Value& v, int* kind, int* record, std::string* why)
{
    const Value* k = v.get("kind");
    const Value* r = v.get("record");
    if (v.kind != Value::TABLE || !k || k->kind != Value::STRING || !r || r->kind != Value::INTEGER) {
        *why = "expected a blade, shield or armor (a table with kind and record)";
        return false;
    }
    if (k->s == "weapon") {
        const Value* blade = v.get("blade");
        if (!blade || !blade->get("record")) {
            *why = "a weapon without its blade";
            return false;
        }
        *kind = PC_PQ_BLADE;
        *record = (int)blade->get("record")->i;
        return true;
    }
    *record = (int)r->i;
    if (k->s == "blade")
        *kind = PC_PQ_BLADE;
    else if (k->s == "shield")
        *kind = PC_PQ_SHIELD;
    else if (k->s == "armor")
        *kind = PC_PQ_ARMOR;
    else {
        *why = "the game combines blades, shields or armor, not " + k->s;
        return false;
    }
    return true;
}

bool combine(const Value& a, const Value& b, pc_pq_combine* out, std::string* why)
{
    int ka, ra, kb, rb;
    if (!combinable(a, &ka, &ra, why) || !combinable(b, &kb, &rb, why))
        return false;
    if (ka != kb) {
        *why = "two of the same kind";
        return false;
    }
    char text[96] = "";
    if (!pc_player_combine || !pc_player_combine(ka, ra, rb, out, text, sizeof text)) {
        *why = pc_player_combine ? text : "no game in this build";
        return false;
    }
    return true;
}

// ---- the per-vsync values and tables ----

enum Query { Q_INVENTORY, Q_WEAPONS, Q_SHIELDS, Q_ARMOUR, Q_ACCESSORIES, Q_EQUIPMENT, Q_WORKSHOP, Q_LABELS,
    Q_CRAFTABLE, Q_COUNT };
const char* const query_names[Q_COUNT] = { "inventory", "weapons", "shields", "armour", "accessories", "equipment",
    "workshop", "labels", "craftable" };

struct Built {
    unsigned long long tick = 0;
    Value value;
} built[Q_COUNT];

Value build(int q)
{
    switch (q) {
    case Q_INVENTORY:
        return inventory_value();
    case Q_WEAPONS:
        return weapons_value();
    case Q_SHIELDS:
        return shields_value();
    case Q_ARMOUR:
        return armor_value(false);
    case Q_ACCESSORIES:
        return armor_value(true);
    case Q_EQUIPMENT:
        return equipment_value();
    case Q_WORKSHOP:
        return workshop_value();
    case Q_LABELS:
        return labels_value();
    default:
        return craftable_value(false);
    }
}

const Value& value_of(int q)
{
    Built& b = built[q];
    if (b.tick != ticks) {
        b.value = build(q);
        b.tick = ticks;
    }
    return b.value;
}

// A value as read-only Lua tables, every level locked.
void push_locked(lua_State* L, const Value& v)
{
    if (v.kind != Value::TABLE) {
        push(L, v);
        return;
    }
    size_t n = v.length();
    lua_createtable(L, (int)n, (int)(v.table.size() - n));
    for (const auto& kv : v.table) {
        push(L, kv.first);
        push_locked(L, kv.second);
        lua_rawset(L, -3);
    }
    lock_table(L);
}

// This vsync's table of query q in this plugin (as lua_api_game.cpp's cached / keep).
int push_query(lua_State* L, int q)
{
    std::string key = std::string("rood.game.") + query_names[q];
    lua_getfield(L, LUA_REGISTRYINDEX, key.c_str());
    if (lua_istable(L, -1)) {
        lua_rawgeti(L, -1, 1);
        bool fresh = (unsigned long long)lua_tointeger(L, -1) == ticks;
        lua_pop(L, 1);
        if (fresh) {
            lua_rawgeti(L, -1, 2);
            lua_remove(L, -2);
            return 1;
        }
    }
    lua_pop(L, 1);
    push_locked(L, value_of(q));
    lua_createtable(L, 2, 0);
    lua_pushinteger(L, (lua_Integer)ticks);
    lua_rawseti(L, -2, 1);
    lua_pushvalue(L, -2);
    lua_rawseti(L, -2, 2);
    lua_setfield(L, LUA_REGISTRYINDEX, key.c_str());
    return 1;
}

int api_inventory(lua_State* L) { return push_query(L, Q_INVENTORY); }
int api_weapons(lua_State* L) { return push_query(L, Q_WEAPONS); }
int api_shields(lua_State* L) { return push_query(L, Q_SHIELDS); }
int api_armour(lua_State* L) { return push_query(L, Q_ARMOUR); }
int api_accessories(lua_State* L) { return push_query(L, Q_ACCESSORIES); }
int api_equipment(lua_State* L) { return push_query(L, Q_EQUIPMENT); }
int api_workshop(lua_State* L) { return push_query(L, Q_WORKSHOP); }
int api_labels(lua_State* L) { return push_query(L, Q_LABELS); }
int api_craftable(lua_State* L) { return push_query(L, Q_CRAFTABLE); }

// What combine reads of an argument, through __index (the queries' tables are
// read-only views): kind, record, and a weapon's blade's record.
Value fields(lua_State* L, int idx)
{
    Value v = Value::new_table();
    lua_getfield(L, idx, "kind");
    if (lua_type(L, -1) == LUA_TSTRING)
        v.set("kind", Value::str(lua_tostring(L, -1)));
    lua_pop(L, 1);
    lua_getfield(L, idx, "record");
    if (lua_isinteger(L, -1))
        v.set("record", Value::integer(lua_tointeger(L, -1)));
    lua_pop(L, 1);
    lua_getfield(L, idx, "blade");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "record");
        if (lua_isinteger(L, -1)) {
            Value blade = Value::new_table();
            blade.set("record", Value::integer(lua_tointeger(L, -1)));
            v.set("blade", blade);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return v;
}

// combine(a, b): a new table each call (its arguments vary); nil and why when the game
// would not combine them.
int api_combine(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TTABLE);
    Value a = fields(L, 1), b = fields(L, 2);
    pc_pq_combine c;
    std::string why;
    if (!combine(a, b, &c, &why)) {
        lua_pushnil(L);
        lua_pushstring(L, why.c_str());
        return 2;
    }
    push_locked(L, combine_value(c, false));
    return 1;
}

void next_tick() { ++ticks; }

struct Register {
    Register()
    {
        tick_hooks().push_back(next_tick);
    }
} register_player;

int fail(char* out, int size, const std::string& message)
{
    std::snprintf(out, (size_t)size, "%s", message.c_str());
    return -1;
}

int answer(char* out, int size, const std::string& json)
{
    if ((int)json.size() >= size)
        return fail(out, size, "the reply is larger than the RPC's line (ask for fewer parts)");
    std::memcpy(out, json.c_str(), json.size() + 1);
    return 0;
}
} // namespace

// rood.game's player queries, added by lua_api_game.cpp's opener to the table on top
// before it is locked.
void open_game_player(lua_State* L)
{
    set_cfunction(L, -1, "inventory", api_inventory);
    set_cfunction(L, -1, "weapons", api_weapons);
    set_cfunction(L, -1, "shields", api_shields);
    set_cfunction(L, -1, "armour", api_armour);
    set_cfunction(L, -1, "accessories", api_accessories);
    set_cfunction(L, -1, "equipment", api_equipment);
    set_cfunction(L, -1, "workshop", api_workshop);
    set_cfunction(L, -1, "labels", api_labels);
    set_cfunction(L, -1, "combine", api_combine);
    set_cfunction(L, -1, "craftable", api_craftable);
}
} // namespace rl

// RPC game_state {parts} and combine_preview {a, b} (dev_rpc.c): the same structures as
// the Lua queries, as JSON. 0 and the result in out, or -1 and the error.
extern "C" int pc_player_rpc(const char* method, const char* params, char* out, int size)
{
    using namespace rl;
    prefab::Json p;
    std::string error;
    if (!prefab::json_parse(params && *params ? params : "{}", p, &error) || p.type != prefab::Json::Object)
        return fail(out, size, "invalid parameters: " + error);
    if (!std::strcmp(method, "combine_preview")) {
        Value a, b;
        const prefab::Json* kind = p.get("kind");
        if (kind && kind->type == prefab::Json::String) {
            // {kind, a, b}: records of that kind
            for (const char* side : { "a", "b" }) {
                const prefab::Json* r = p.get(side);
                if (!r || r->type != prefab::Json::Number)
                    return fail(out, size, "combine_preview {kind, a, b}: a and b are record numbers");
                Value v = Value::new_table();
                v.set("kind", Value::str(kind->string));
                v.set("record", Value::integer((long long)r->number));
                (side[0] == 'a' ? a : b) = v;
            }
        } else {
            const prefab::Json* ja = p.get("a");
            const prefab::Json* jb = p.get("b");
            if (!ja || !jb)
                return fail(out, size, "combine_preview needs {kind, a, b} or {a: {kind, record}, b: {...}}");
            a = from_json(*ja);
            b = from_json(*jb);
        }
        pc_pq_combine c;
        if (!combine(a, b, &c, &error))
            return fail(out, size, error);
        return answer(out, size, json(combine_value(c, true), false));
    }
    // game_state: every part asked for (default all but craftable), as this vsync's values
    std::vector<int> wanted;
    const prefab::Json* parts = p.get("parts");
    if (parts && parts->type == prefab::Json::Array) {
        for (const prefab::Json& item : parts->items) {
            int q = -1;
            for (int i = 0; i < Q_COUNT; ++i)
                if (item.type == prefab::Json::String && item.string == query_names[i])
                    q = i;
            if (q < 0)
                return fail(out, size,
                    "unknown part (inventory, weapons, shields, armour, accessories, equipment, workshop, labels, "
                    "craftable)");
            wanted.push_back(q);
        }
    } else {
        for (int i = 0; i < Q_CRAFTABLE; ++i)
            wanted.push_back(i);
    }
    // fresh values (the RPC also runs while the game is paused, after writes)
    Value t = Value::new_table();
    std::vector<bool> nil(Q_COUNT, false);
    for (int q : wanted) {
        Value v = build(q);
        nil[q] = v.kind == Value::NIL;
        t.set(query_names[q], v);
    }
    std::string text = json(t, false);
    // a part that is nil (no player, no workshop) is null, not left out
    for (int q : wanted)
        if (nil[q])
            text.insert(text.size() - 1, std::string(text.size() > 2 ? "," : "") + "\"" + query_names[q] + "\":null");
    return answer(out, size, text);
}
