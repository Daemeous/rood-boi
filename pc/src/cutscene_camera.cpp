// Cutscene free-flight controls. Run on every presented image, not just while F1 is open.
// The photo camera owns all view changes and restores the borrowed editor camera on finish.
#include "cutscene_internal.h"
#include "photo.h"
#include "imgui.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
extern "C" {
#include "boot_controller.h"
int pc_overlay_look_delta(float* dx, float* dy);
int pc_ui_shell_wants_mouse(void);
int pc_window_pad_captured(void);
}
namespace {
constexpr const char* owner = "cutscene.camera";
bool flying;
float speed = 600;
float deadzone(float x)
{
    return std::fabs(x) <= 0.15f ? 0 : std::copysign((std::fabs(x) - 0.15f) / 0.85f, x);
}
}
extern "C" int pc_cutscene_camera_flying(void)
{
    return cutscene::free_camera_on() && flying;
}
extern "C" void pc_cutscene_camera_input(void)
{
    flying = false;
    if (!cutscene::free_camera_on())
        return;
    char current[128];
    if (!pc_photo_camera_owner(current, sizeof current) || std::strcmp(current, owner)) {
        // Escape/Start or an external release may have given the camera back already.
        cutscene::free_camera(false, nullptr);
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (!SDL_GetKeyboardFocus() || io.WantTextInput)
        return;
    flying = !io.WantCaptureMouse && !pc_ui_shell_wants_mouse()
        && ImGui::IsMouseDown(ImGuiMouseButton_Right);
    float dt = std::max(0.0f, std::min(io.DeltaTime, 0.1f));
    bool pad_captured = pc_photo_input_captured || pc_window_pad_captured();
    float left[2] = {}, right[2] = {};
    if (!pad_captured) {
        pc_controller_left_stick(left);
        pc_controller_right_stick(right);
    }
    float sideways = deadzone(left[0]), forward = -deadzone(left[1]);
    float yaw = deadzone(right[0]) * 90 * dt, pitch = -deadzone(right[1]) * 90 * dt;
    unsigned buttons = pad_captured ? 0 : pc_controller_buttons();
    auto trigger = [&](int which) {
        if (pad_captured)
            return 0.0f;
        float value = pc_controller_trigger(which);
        return value >= 0 ? value : ((buttons & (1u << which)) ? 1.0f : 0.0f);
    };
    float up = trigger(1) - trigger(0);
    if (flying) {
        float dx = io.MouseDelta.x, dy = io.MouseDelta.y;
        pc_overlay_look_delta(&dx, &dy);
        float sensitivity = 0.12f * 1080 / std::max(io.DisplaySize.y, 1.0f);
        yaw += dx * sensitivity;
        pitch -= dy * sensitivity;
        sideways += (float)ImGui::IsKeyDown(ImGuiKey_D) - ImGui::IsKeyDown(ImGuiKey_A);
        forward += (float)ImGui::IsKeyDown(ImGuiKey_W) - ImGui::IsKeyDown(ImGuiKey_S);
        up += (float)ImGui::IsKeyDown(ImGuiKey_E) - ImGui::IsKeyDown(ImGuiKey_Q);
        if (io.MouseWheel)
            speed = std::max(50.0f, std::min(8000.0f, speed * std::pow(1.25f, io.MouseWheel)));
    }
    if (!yaw && !pitch && !sideways && !forward && !up)
        return;
    pc_photo_pose pose;
    if (!pc_photo_camera_get(&pose))
        return;
    // Fly rather than orbit: yaw/pitch keep the eye fixed and put the target ahead.
    pose.yaw += yaw;
    pose.pitch = std::max(-89.0f, std::min(89.0f, pose.pitch + pitch));
    float y = pose.yaw * 0.01745329252f, p = pose.pitch * 0.01745329252f;
    float distance = speed * dt * ((flying && io.KeyShift) ? 3 : 1);
    pose.position[0] += (std::cos(y) * sideways + std::sin(y) * std::cos(p) * forward) * distance;
    pose.position[1] -= (std::sin(p) * forward + up) * distance; // Q/E and triggers: world vertical
    pose.position[2] += (-std::sin(y) * sideways + std::cos(y) * std::cos(p) * forward) * distance;
    pc_photo_camera_set(owner, &pose, PC_POSE_POSITION | PC_POSE_YAW | PC_POSE_PITCH, nullptr, 0);
}
