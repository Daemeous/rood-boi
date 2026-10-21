// rood.camera's host side (photo.h; pc/docs/Extensions.md "Photo mode foundation"): the editor's live
// free camera (editor_view.cpp) borrowed while the game is paused, its pose as a plugin sets it (an orbit
// about a target, view-space moves, roll, field of view, optional collision with the room); every change
// asks for a render of the frozen moment (photo.cpp).
#include "photo.h"
#include "photo_internal.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "hud/game_query.h"
#include "scene_capture.h"

extern "C" {
/* editor_view.cpp: the live free camera, borrowed */
int pc_editor_photo_begin(void) __attribute__((weak));
void pc_editor_photo_end(void) __attribute__((weak));
int pc_editor_photo_get(float eye[3], float* yaw, float* pitch, float* roll, float* fov) __attribute__((weak));
void pc_editor_photo_set(const float eye[3], float yaw, float pitch, float roll, float fov) __attribute__((weak));
float pc_camera_stick_ray_triangle(const float o[3], const float d[3], const float a[3], const float b[3],
    const float c[3], float t0, float t1) __attribute__((weak));
}

namespace {
using photo::contains;
using photo::copy_out;
using photo::notify;
constexpr float PI_F = 3.14159265f;

std::string camera_owner;
float target[3];
bool collision;

void forward(float yaw, float pitch, float f[3])
{
    f[0] = cosf(pitch) * sinf(yaw);
    f[1] = -sinf(pitch);
    f[2] = cosf(pitch) * cosf(yaw);
}
/* editor_view.cpp's basis: forward, right, up (world up is -y). */
void basis(float yaw, float pitch, float f[3], float r[3], float u[3])
{
    forward(yaw, pitch, f);
    const float up[3] = { 0, -1, 0 };
    r[0] = f[1] * up[2] - f[2] * up[1];
    r[1] = f[2] * up[0] - f[0] * up[2];
    r[2] = f[0] * up[1] - f[1] * up[0];
    float l = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (int k = 0; k < 3; k++)
        r[k] /= l > 1e-6f ? l : 1;
    u[0] = r[1] * f[2] - r[2] * f[1];
    u[1] = r[2] * f[0] - r[0] * f[2];
    u[2] = r[0] * f[1] - r[1] * f[0];
}
float length3(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
/* Collision: the eye pulled toward the target in front of the nearest solid room surface between them
 * (camera_stick.c's test: drawn, not disabled, not the alternate set). */
void collide(float eye[3])
{
    if (!collision || !pc_camera_stick_ray_triangle)
        return;
    float d[3] = { eye[0] - target[0], eye[1] - target[1], eye[2] - target[2] };
    float len = length3(d);
    if (len < 1)
        return;
    for (float& c : d)
        c /= len;
    int count = 0;
    const scene_triangle* t = pc_scene_triangle_data(&count);
    float best = -1;
    for (int i = 0; t && i < count; i++) {
        if (t[i].flags & 0x9100)
            continue;
        float hit = pc_camera_stick_ray_triangle(target, d, t[i].vertex[0].position, t[i].vertex[1].position,
            t[i].vertex[2].position, 1, best > 0 ? best : len);
        if (hit > 0)
            best = hit;
    }
    if (best > 0) {
        float at = fmaxf(best - 24, 1);
        for (int k = 0; k < 3; k++)
            eye[k] = target[k] + d[k] * at;
    }
}
bool camera_mine(const char* owner, char* why, int size)
{
    if (camera_owner.empty()) {
        copy_out(why, size, "the camera is not begun (rood.camera.begin)");
        return false;
    }
    if (camera_owner != owner) {
        copy_out(why, size, "the camera belongs to " + camera_owner);
        return false;
    }
    return true;
}

} // namespace

/* ------------------------------------------------------------ camera */

extern "C" int pc_photo_camera_begin(const char* owner, char* why, int size)
{
    if (!camera_owner.empty()) {
        if (camera_owner == owner)
            return 0;
        copy_out(why, size, "the camera belongs to " + camera_owner);
        return -1;
    }
    if (!photo::paused()) {
        copy_out(why, size, "the game is not paused (rood.game.pause first)");
        return -1;
    }
    pc_game_query q;
    memset(&q, 0, sizeof q);
    if (pc_game_query_fill)
        pc_game_query_fill(&q);
    if (!q.field) {
        copy_out(why, size, "no room to look at");
        return -1;
    }
    if (!pc_editor_photo_begin || !pc_editor_photo_begin()) {
        copy_out(why, size, "the camera needs the hardware renderer");
        return -1;
    }
    camera_owner = owner;
    collision = false;
    /* The orbit's target: Ashley (her middle) when she is in view, else a point ahead. */
    float eye[3], yaw, pitch, roll, fov, f[3];
    pc_editor_photo_get(eye, &yaw, &pitch, &roll, &fov);
    forward(yaw, pitch, f);
    float distance = 1500;
    if (q.ashley) {
        float a[3] = { (float)q.x - eye[0], (float)q.y - 90 - eye[1], (float)q.z - eye[2] };
        float along = a[0] * f[0] + a[1] * f[1] + a[2] * f[2];
        if (along > 64)
            distance = along;
    }
    for (int k = 0; k < 3; k++)
        target[k] = eye[k] + f[k] * distance;
    pc_photo_redraw();
    return 0;
}
extern "C" void pc_photo_camera_finish(const char* owner)
{
    if (camera_owner.empty() || camera_owner != owner)
        return;
    if (pc_editor_photo_end)
        pc_editor_photo_end();
    camera_owner.clear();
    pc_photo_redraw(); /* the game's camera at M again */
}
extern "C" int pc_photo_camera_owner(char* out, int size)
{
    copy_out(out, size, camera_owner);
    return !camera_owner.empty();
}
extern "C" int pc_photo_camera_get(pc_photo_pose* out)
{
    memset(out, 0, sizeof *out);
    float yaw, pitch;
    if (camera_owner.empty() || !pc_editor_photo_get
        || !pc_editor_photo_get(out->position, &yaw, &pitch, &out->roll, &out->fov))
        return 0;
    out->yaw = yaw * 180 / PI_F, out->pitch = pitch * 180 / PI_F;
    memcpy(out->target, target, sizeof target);
    out->collision = collision;
    return 1;
}
extern "C" int pc_photo_camera_set(const char* owner, const pc_photo_pose* pose, unsigned mask, char* why, int size)
{
    if (!camera_mine(owner, why, size))
        return -1;
    float eye[3], yaw, pitch, roll, fov, f[3];
    pc_editor_photo_get(eye, &yaw, &pitch, &roll, &fov);
    forward(yaw, pitch, f);
    float to[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
    float distance = fmaxf(length3(to), 1);
    if (mask & PC_POSE_POSITION)
        memcpy(eye, pose->position, sizeof eye);
    if (mask & PC_POSE_YAW)
        yaw = pose->yaw * PI_F / 180;
    if (mask & PC_POSE_PITCH)
        pitch = fmaxf(-89, fminf(89, pose->pitch)) * PI_F / 180;
    if (mask & PC_POSE_TARGET) {
        memcpy(target, pose->target, sizeof target);
        if (!(mask & (PC_POSE_YAW | PC_POSE_PITCH))) { /* look at it */
            float d[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
            float l = length3(d);
            if (l > 1e-3f) {
                yaw = atan2f(d[0], d[2]);
                pitch = asinf(fmaxf(-1, fminf(1, -d[1] / l)));
            }
        }
    } else if (mask & (PC_POSE_POSITION | PC_POSE_YAW | PC_POSE_PITCH)) { /* the target stays ahead */
        forward(yaw, pitch, f);
        for (int k = 0; k < 3; k++)
            target[k] = eye[k] + f[k] * distance;
    }
    if (mask & PC_POSE_ROLL)
        roll = fmaxf(-180, fminf(180, pose->roll));
    if (mask & PC_POSE_FOV)
        fov = fmaxf(5, fminf(120, pose->fov));
    if (mask & PC_POSE_COLLISION)
        collision = pose->collision != 0;
    collide(eye);
    pc_editor_photo_set(eye, yaw, pitch, roll, fov);
    pc_photo_redraw();
    return 0;
}
extern "C" int pc_photo_camera_orbit(const char* owner, float dyaw, float dpitch, float zoom, char* why, int size)
{
    if (!camera_mine(owner, why, size))
        return -1;
    float eye[3], yaw, pitch, roll, fov, f[3];
    pc_editor_photo_get(eye, &yaw, &pitch, &roll, &fov);
    float to[3] = { eye[0] - target[0], eye[1] - target[1], eye[2] - target[2] };
    float distance = fmaxf(length3(to), 1) * (zoom > 0 ? zoom : 1);
    distance = fmaxf(16, fminf(20000, distance));
    yaw += dyaw * PI_F / 180;
    pitch = fmaxf(-1.55f, fminf(1.55f, pitch + dpitch * PI_F / 180));
    forward(yaw, pitch, f);
    for (int k = 0; k < 3; k++)
        eye[k] = target[k] - f[k] * distance;
    collide(eye);
    pc_editor_photo_set(eye, yaw, pitch, roll, fov);
    pc_photo_redraw();
    return 0;
}
extern "C" int pc_photo_camera_move(const char* owner, float right, float up, float ahead, char* why, int size)
{
    if (!camera_mine(owner, why, size))
        return -1;
    float eye[3], yaw, pitch, roll, fov, f[3], r[3], u[3];
    pc_editor_photo_get(eye, &yaw, &pitch, &roll, &fov);
    basis(yaw, pitch, f, r, u);
    for (int k = 0; k < 3; k++) {
        float d = r[k] * right + u[k] * up + f[k] * ahead;
        eye[k] += d;
        target[k] += d;
    }
    pc_editor_photo_set(eye, yaw, pitch, roll, fov);
    pc_photo_redraw();
    return 0;
}


namespace {
void release(const std::string& owner) { pc_photo_camera_finish(owner.c_str()); }
void owners(std::vector<std::string>& out)
{
    if (!camera_owner.empty())
        out.push_back(camera_owner);
}
void reset()
{
    if (!camera_owner.empty() && pc_editor_photo_end)
        pc_editor_photo_end();
    camera_owner.clear();
}
void json(std::string& out)
{
    pc_photo_pose pose;
    if (!pc_photo_camera_get(&pose)) {
        out += ",\"camera\":null";
        return;
    }
    char text[320];
    snprintf(text, sizeof text,
        ",\"camera\":{\"owner\":%s,\"position\":[%.1f,%.1f,%.1f],\"target\":[%.1f,%.1f,%.1f],\"yaw\":%.2f,"
        "\"pitch\":%.2f,\"roll\":%.2f,\"fov\":%.2f,\"collision\":%s}",
        photo::json_quote(camera_owner).c_str(), pose.position[0], pose.position[1], pose.position[2], pose.target[0],
        pose.target[1], pose.target[2], pose.yaw, pose.pitch, pose.roll, pose.fov, pose.collision ? "true" : "false");
    out += text;
}
photo::Register part({ "camera", release, owners, reset, json, nullptr });
} // namespace
