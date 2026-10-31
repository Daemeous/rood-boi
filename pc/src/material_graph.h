// Node material graphs (pc/docs/plans/2026-10-30-node-materials-plan.md): node
// definitions (small GLSL functions with declared inputs and outputs, one JSON
// file each), graph masters (a master file with a `graph`), GLSL generation
// into the generated include material_graph.glsl (read by native_frag.glsl,
// hot reloaded by shader_reload.cpp), the graph slot each assigned instance of
// a graph master gets (the seventh material plane), and RPC material_graph.
#ifndef ROOD_MATERIAL_GRAPH_H
#define ROOD_MATERIAL_GRAPH_H
#include <map>
#include <string>
#include <vector>
namespace material_graph {
enum Type { T_FLOAT = 1, T_FLOAT2 = 2, T_FLOAT3 = 3, T_FLOAT4 = 4 };
struct Port {
    std::string name, label;
    Type type = T_FLOAT;
    std::vector<float> def; /* the knob's default (type's size); empty on an output */
    float lo = 0, hi = 1;
    bool pin = false;      /* pinned on the node body by default */
    bool color = false;    /* a float3 knob shown as a swatch */
    bool optional = false; /* (output node) unlinked: leaves the value as it is */
    bool knob = true;      /* false: an output, or a linkable-only input */
    bool time = false;     /* (node file `"time": true`) unlinked, the input reads the game time in seconds */
};
struct NodeDef {
    std::string name, label, category, glsl, path;
    std::vector<Port> inputs, outputs;
    bool builtin = false; /* the sources and the output: generated, not a function */
    bool user = false;
    const Port* input(const std::string& n) const;
    const Port* output(const std::string& n) const;
};
struct Node {
    std::string id, type;
    float x = 0, y = 0;
    std::map<std::string, std::vector<float>> knobs; /* input name -> value (the default when absent) */
    std::vector<std::string> pinned;                 /* input names pinned on the body (absent: the definition's) */
    bool pinned_set = false;
};
struct Link {
    std::string from_node, from_port, to_node, to_port;
};
struct Graph {
    std::string name, path;
    std::vector<Node> nodes;
    std::vector<Link> links;
    std::map<std::string, std::string> promoted; /* "node.input" -> parameter name */
    bool user = false, dirty = false;
    Node* node(const std::string& id);
    const Node* node(const std::string& id) const;
};
struct Generated {
    bool ok = false;
    std::string glsl, error, error_node;
    std::vector<std::string> params; /* the master's parameters, in order */
};
/* The node library (shipped, then the user's; the built-ins first). */
const std::vector<NodeDef>& nodes();
const NodeDef* node_def(const std::string& name);
std::string nodes_dir();      /* $ROOD_MATERIAL_NODES, else nodes/ beside the shipped masters */
std::string user_nodes_dir(); /* <user maps layer>/nodes */
/* The graphs: every master with a `graph` (shipped and the user's), by name. */
std::vector<std::string> graph_names();
Graph* graph(const std::string& name);
Graph fresh_graph(const std::string& name); /* texel -> output */
/* The GLSL of one graph as a function `graph_<name>` (no switch): the
 * validation the editor reports; `error_node` names the node at fault. */
Generated generate_one(const Graph& g);
/* The whole include (every graph, the slot switch); `only_text`: no file write. */
std::string generate_all(std::string* first_error);
bool save_graph(const std::string& name, std::string* error); /* into graphs_dir(), the parameters derived */
bool remove_graph(const std::string& name, std::string* error);
void reload(); /* re-read nodes and graphs on the next look */
std::string generated_dir(); /* $ROOD_SHADERS_GENERATED, else pc/build/shaders/generated */
std::string generated_path();
} // namespace material_graph
extern "C" {
/* The graph slot of an assigned instance (1..255), 0 for none (its master is
 * not a graph, or there are more than 255). The planes write it per texel. */
int pc_material_graph_slot(const char* instance_id);
/* Once per frame: regenerates the include when the graphs, the instances or
 * their values changed (the hot reload picks the file up). */
void pc_material_graph_poll(void);
int pc_material_graph_rpc(const char* params, char* out, int size);
/* The values table the generated include reads (vec4s; [0].x the game time in seconds): `count` vec4s.
 * The native draws upload it with every frame (vk_render.inc binding 7): a knob or parameter edit shows
 * on the next frame without regenerating the include. */
const float* pc_material_graph_values(int* count);
}
#endif
