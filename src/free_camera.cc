/*
 * Free Camera for Pokemon Alpha Sapphire
 * Derived from Sango Plugin, Copyright (C) 2026  David Darras
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file free_camera.cc
 * @brief An orbit camera for the overworld, moved by the right stick.
 *
 * The three hooks and the way to replace the camera of the game come from
 * overworld/camera.cc of Sango Plugin (the "Tps" and "Free" modes). The
 * difference: here the angle of the camera is a value that only the player
 * changes. The camera never reads the direction of the character to turn.
 *
 * The declarations are in free_camera.h.
 */

#include "free_camera.h"

#include <3ds.h>

#include <cmath>

#include "core/hook.h"
#include "core/native/process_manager.h"
#include "overworld/address.h"
#include "overworld/constant/facing.h"
#include "overworld/native/model_manager.h"
#include "overworld/native/renderer.h"
#include "overworld/native/stereo_camera.h"
#include "renderer/address.h"
#include "system/native/controller.h"

namespace freecam {

// ---------------------------------------------------------------------------
// The settings. Change these values to change the feel of the camera.
// The units are the units of the world of the game (a character is about 30
// to 40 units high). The angles are in radians.
// ---------------------------------------------------------------------------
namespace {

/// The distance between the camera and the player.
constexpr f32 kDistance = 260.0f;
/// The height above the feet of the player that the camera looks at.
constexpr f32 kLookHeight = 24.0f;
/// The angle above the ground when the camera starts (0.55 = about 31 degrees).
constexpr f32 kStartPitch = 0.55f;
/// The lowest and the highest angle above the ground.
constexpr f32 kMinPitch = 0.10f;
constexpr f32 kMaxPitch = 1.40f;

/// The turn speed of the camera at full tilt, in radians for each frame.
constexpr f32 kYawSpeed = 0.045f;
constexpr f32 kPitchSpeed = 0.030f;
/// The part of the stick range that does nothing (0 to 1).
constexpr f32 kDeadZone = 0.20f;
/// true: push the stick up to move the camera up (inverted look).
constexpr bool kInvertPitch = false;
/// true: L and R turn the camera, ZL and ZR tilt it. This is the control of
/// the consoles that have no right stick. L + R together reset the camera.
constexpr bool kUseShoulderButtons = true;

/// The range of the C-Stick that the system gives (about -156 to 156).
constexpr f32 kStickRange = 150.0f;

// ---------------------------------------------------------------------------

constexpr f32 kPi = 3.14159265358979f;
/// The yaw of the default camera of the game: it is south of the player and
/// looks to the north.
constexpr f32 kDefaultYaw = kPi / 2.0f;

// The state of the camera. Constant start values: no guard at the first use.
/// The angle of the camera around the player, on the ground.
/// 0: the camera is east of the player. kPi / 2: south (the game default).
f32 yaw = kDefaultYaw;
/// The angle of the camera above the ground.
f32 pitch = kStartPitch;
/// true: the camera must go behind the player at the next frame.
bool needs_reset = true;
/// true: the next call of the look-at function is the camera of the overworld.
bool is_updating_camera = false;
/// true: the C-Stick is ready.
bool is_cstick_ready = false;

core::Hook<u32(overworld::StereoCamera*, bool)> update_matrices_hook;
core::Hook<Mtx34*(Mtx34*, Vec3*, Vec3*, Vec3*)> update_look_at_hook;
core::Hook<u32(uptr, Vec3*, f32*, overworld::Facing*, u8*)>
    get_player_movement_hook;

// The 8 directions, from the east, counterclockwise (east, north-east...).
const overworld::Facing kDirectionTable[8] = {
    overworld::Facing::kRight,    overworld::Facing::kUpRight,
    overworld::Facing::kUp,       overworld::Facing::kUpLeft,
    overworld::Facing::kLeft,     overworld::Facing::kDownLeft,
    overworld::Facing::kDown,     overworld::Facing::kDownRight};

f32 Clamp(f32 value, f32 low, f32 high) {
  return value < low ? low : (value > high ? high : value);
}

// Removes the dead zone and makes the small moves finer. The input and the
// output go from -1 to 1.
f32 ShapeStick(f32 value) {
  const f32 magnitude = std::fabs(value);
  if (magnitude <= kDeadZone) return 0.0f;
  f32 scaled = (magnitude - kDeadZone) / (1.0f - kDeadZone);
  if (scaled > 1.0f) scaled = 1.0f;
  scaled = 0.5f * scaled + 0.5f * scaled * scaled;
  return value < 0.0f ? -scaled : scaled;
}

// Puts the camera behind the player. Returns false when the characters of the
// map do not exist yet: the caller tries again at the next frame.
bool ResetBehindPlayer() {
  overworld::ModelManager* models = overworld::ModelManager::GetInstanceOrNull();
  if (models == nullptr) return false;

  pitch = kStartPitch;
  yaw = kDefaultYaw;
  const Vec3& facing = models->GetPlayer().facing_direction;
  const f32 length = std::sqrt(facing.x * facing.x + facing.z * facing.z);
  if (length >= 0.001f) {
    // The camera is on the opposite side of the direction of the player.
    yaw = std::atan2(-facing.z / length, -facing.x / length);
  }
  return true;
}

// Gives the 8-direction value of a vector (east, north).
overworld::Facing QuantizeDirection(f32 east, f32 north) {
  const f32 angle = std::atan2(north, east); // -pi to pi, 0 = east
  s32 sector = static_cast<s32>(std::floor(angle / (kPi / 4.0f) + 0.5f));
  sector &= 7;
  return kDirectionTable[sector];
}

// Gives the vector (east, north) of an 8-direction value. Returns false for
// a value that is not a direction.
bool FacingToVector(overworld::Facing facing, f32* east, f32* north) {
  const f32 d = 0.70710678f;
  switch (facing) {
    case overworld::Facing::kUp: *east = 0; *north = 1; return true;
    case overworld::Facing::kDown: *east = 0; *north = -1; return true;
    case overworld::Facing::kLeft: *east = -1; *north = 0; return true;
    case overworld::Facing::kRight: *east = 1; *north = 0; return true;
    case overworld::Facing::kUpLeft: *east = -d; *north = d; return true;
    case overworld::Facing::kUpRight: *east = d; *north = d; return true;
    case overworld::Facing::kDownLeft: *east = -d; *north = -d; return true;
    case overworld::Facing::kDownRight: *east = d; *north = -d; return true;
    default: return false;
  }
}

// Turns a direction of the screen (east, north) by the angle of the camera:
// "up" becomes the direction of the view (from the camera to the player), and
// "right" the view turned to the right. The world has x to the east and z to
// the south, so north = -z and the view is (-cos(yaw), sin(yaw)).
void TurnByCamera(f32 east, f32 north, f32 camera_yaw, f32* world_east,
                  f32* world_north) {
  const f32 view_east = -std::cos(camera_yaw);
  const f32 view_north = std::sin(camera_yaw);
  *world_east = north * view_east + east * view_north;
  *world_north = north * view_north - east * view_east;
}

// Called by the game each time that it updates its camera.
u32 UpdateMatricesHook(overworld::StereoCamera* stereo_camera, bool update) {
  auto& process_manager = core::ProcessManager::GetInstance();
  if (process_manager.IsCurrentProcess(overworld::address::kVtable)) {
    auto* renderer = overworld::Renderer::GetInstance();
    if (renderer != nullptr && renderer->GetStereoCamera() == stereo_camera) {
      is_updating_camera = true;
    }
  }
  return update_matrices_hook(stereo_camera, update);
}

// The game gives its position, its up vector and its target to this function
// to make the matrix of the camera. The hook replaces them.
Mtx34* UpdateLookAtHook(Mtx34* output, Vec3* pos, Vec3* up, Vec3* target) {
  if (!is_updating_camera) {
    return update_look_at_hook(output, pos, up, target);
  }
  is_updating_camera = false;

  overworld::ModelManager* models = overworld::ModelManager::GetInstanceOrNull();
  if (models == nullptr) {
    return update_look_at_hook(output, pos, up, target);
  }
  const Vec3 player = models->GetPlayer().draw_pos;

  // The camera looks at the player, and turns around him.
  const Vec3 focus(player.x, player.y + kLookHeight, player.z);
  const f32 flat = kDistance * std::cos(pitch);
  pos->x = focus.x + flat * std::cos(yaw);
  pos->y = focus.y + kDistance * std::sin(pitch);
  pos->z = focus.z + flat * std::sin(yaw);
  *target = focus;
  *up = Vec3(0.0f, 1.0f, 0.0f);

  return update_look_at_hook(output, pos, up, target);
}

// The game asks where the player must move. The stick of the game gives a
// direction of the screen: up is the north. The hook turns this direction by
// the angle of the camera, so that up goes away from the camera.
u32 GetPlayerMovementHook(uptr self, Vec3* dir_vec, f32* speed,
                          overworld::Facing* dir, u8* x) {
  const u32 result = get_player_movement_hook(self, dir_vec, speed, dir, x);
  if (dir_vec == nullptr || dir == nullptr) return result;
  if (*dir == overworld::Facing::kInvalid ||
      static_cast<u8>(*dir) >= static_cast<u8>(overworld::Facing::kCount)) {
    return result; // the player does not move
  }
  // The default camera needs no change.
  if (std::fabs(yaw - kDefaultYaw) < 0.0001f) return result;

  // The direction that the player asks: (east, north).
  f32 east = dir_vec->x;
  f32 north = dir_vec->z;
  f32 length = std::sqrt(east * east + north * north);
  if (length < 0.001f) {
    if (!FacingToVector(*dir, &east, &north)) return result;
    length = 1.0f;
  } else {
    east /= length;
    north /= length;
  }

  f32 world_east = 0.0f;
  f32 world_north = 0.0f;
  TurnByCamera(east, north, yaw, &world_east, &world_north);

  dir_vec->x = world_east * length;
  dir_vec->z = world_north * length;
  *dir = QuantizeDirection(world_east, world_north);
  return result;
}

// Reads the C-Stick. The values go from -1 to 1 (right and up are positive).
void ReadCStick(f32* stick_x, f32* stick_y) {
  *stick_x = 0.0f;
  *stick_y = 0.0f;
  if (!is_cstick_ready) return;
  irrstScanInput();
  circlePosition position;
  irrstCstickRead(&position);
  *stick_x = Clamp(static_cast<f32>(position.dx) / kStickRange, -1.0f, 1.0f);
  *stick_y = Clamp(static_cast<f32>(position.dy) / kStickRange, -1.0f, 1.0f);
}

} // namespace

void Initialize() {
  // The C-Stick of the New 3DS (or the Circle Pad Pro) is a device of the
  // system, not of the game: the plugin reads it with the service ir:rst.
  // When it fails, the buttons L, R, ZL and ZR still work.
  if (R_SUCCEEDED(srvInit()) && R_SUCCEEDED(irrstInit())) {
    is_cstick_ready = true;
  }

  update_matrices_hook.Install(renderer::address::kStereoCameraUpdateMatrices,
                               UpdateMatricesHook);
  update_look_at_hook.Install(renderer::address::kStereoCameraUpdateLookAt,
                              UpdateLookAtHook);
  get_player_movement_hook.Install(overworld::address::kGetPlayerMovement,
                                   GetPlayerMovementHook,
                                   overworld::address::kVtable);
}

void OnProcessLoad(uptr vtable) {
  // The game loaded the code of a process again: write the hooks again.
  core::HookBase::OnProcessLoad(vtable);
  // A new overworld (a new map, or the end of a battle): the camera starts
  // behind the player.
  if (vtable != 0 && vtable == overworld::address::kVtable) {
    needs_reset = true;
  }
}

void Update() {
  if (!core::ProcessManager::IsOverworldActive()) return;

  f32 yaw_input = 0.0f;   // positive: the view turns to the right
  f32 pitch_input = 0.0f; // positive: the view goes up

  f32 stick_x = 0.0f;
  f32 stick_y = 0.0f;
  ReadCStick(&stick_x, &stick_y);
  yaw_input += ShapeStick(stick_x);
  pitch_input += ShapeStick(stick_y);

  auto& controller = sys::Controller::GetInstance();
  const bool is_left = controller.IsKeyDown(Key::kL);
  const bool is_right = controller.IsKeyDown(Key::kR);
  if (is_left && is_right) needs_reset = true;
  if (kUseShoulderButtons) {
    if (is_left) yaw_input -= 1.0f;
    if (is_right) yaw_input += 1.0f;
    if (controller.IsKeyDown(Key::kZr)) pitch_input += 1.0f;
    if (controller.IsKeyDown(Key::kZl)) pitch_input -= 1.0f;
  }

  if (needs_reset && ResetBehindPlayer()) needs_reset = false;

  // The view turns to the right when the camera turns clockwise on the map:
  // yaw grows. The view goes up when the camera goes down: pitch falls.
  yaw += Clamp(yaw_input, -1.0f, 1.0f) * kYawSpeed;
  const f32 pitch_direction = kInvertPitch ? 1.0f : -1.0f;
  pitch += pitch_direction * Clamp(pitch_input, -1.0f, 1.0f) * kPitchSpeed;
  pitch = Clamp(pitch, kMinPitch, kMaxPitch);

  // Keep the yaw small, so that the float keeps its precision.
  if (yaw > kPi) yaw -= 2.0f * kPi;
  if (yaw < -kPi) yaw += 2.0f * kPi;
}

} // namespace freecam
