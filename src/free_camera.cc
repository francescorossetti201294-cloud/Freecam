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
 * The hooks and the way to replace the camera of the game come from
 * overworld/camera.cc of Sango Plugin (the "Tps" and "Free" modes). The
 * difference: here the angle of the camera is a value that only the player
 * changes. The camera never reads the direction of the character to turn.
 *
 * The camera does not choose a place for itself. It starts from the camera of
 * the game (its target, its distance and its angles) and adds the turn of the
 * player. With no turn, the plugin gives the camera of the game back as it is:
 * the scenes with their own camera (the start of a new game, the events, the
 * ROM hacks with other settings) stay right.
 *
 * To walk relative to the camera, the plugin turns the position of the Circle
 * Pad that the game reads, and it does nothing to the result of the game. The
 * game keeps its own state of the walk (the direction, the steps, the doors):
 * it must see the stick that the player would have to push.
 *
 * The declarations are in free_camera.h.
 */

#include "free_camera.h"

#include <3ds.h>

#include <cmath>

#include "core/hook.h"
#include "core/native/process_manager.h"
#include "overworld/address.h"
#include "overworld/native/renderer.h"
#include "overworld/native/stereo_camera.h"
#include "renderer/address.h"
#include "system/address.h"
#include "system/native/controller.h"

namespace freecam {

// ---------------------------------------------------------------------------
// The settings. Change these values to change the feel of the camera.
// The units are the units of the world of the game (a character is about 30
// to 40 units high). The angles are in radians.
// ---------------------------------------------------------------------------
namespace {

/// The distance of the camera is the distance of the game, multiplied by this
/// number (1.0 = the same distance, 1.3 = farther).
constexpr f32 kDistanceScale = 1.0f;
/// The camera goes at most this low and this high above the ground (radians).
/// The angle of the game itself is always allowed.
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
/// How far the camera can turn away from the view of the game, in radians.
/// The maps of the game are made like a stage: the back of the houses is not
/// modeled. 1.22 (70 degrees) keeps most of these holes out of the view.
/// Use 3.14159 for a full turn (the camera can see the back of everything).
constexpr f32 kYawLimit = 1.22f;
/// Test option. false: the Circle Pad is turned only when the game reads it to
/// walk. true: it is turned all the time in the overworld (the menus and the
/// dialogs of the overworld then see a turned stick).
constexpr bool kTurnStickEverywhere = false;

/// The range of the C-Stick that the system gives (about -156 to 156).
constexpr f32 kStickRange = 150.0f;

// ---------------------------------------------------------------------------

constexpr f32 kPi = 3.14159265358979f;
/// The yaw of the default camera of the game: it is south of the player and
/// looks to the north.
constexpr f32 kDefaultYaw = kPi / 2.0f;

// The state of the camera. Constant start values: no guard at the first use.
/// The turn that the player added to the camera of the game, on the ground.
/// 0: the view of the game. It grows when the view turns to the right.
f32 yaw_delta = 0.0f;
/// The tilt that the player added to the angle of the camera above the ground.
f32 pitch_delta = 0.0f;
/// The angle above the ground of the last camera of the game.
f32 game_pitch_seen = 0.0f;
/// true: the next call of the look-at function is the camera of the overworld.
bool is_updating_camera = false;
/// true: the C-Stick is ready.
bool is_cstick_ready = false;
/// true: the game is inside the function that reads the controls to walk.
bool is_reading_movement = false;
/// true: the overworld is the current process of the game.
bool is_overworld_active = false;

core::Hook<u32(overworld::StereoCamera*, bool)> update_matrices_hook;
core::Hook<Mtx34*(Mtx34*, Vec3*, Vec3*, Vec3*)> update_look_at_hook;
core::Hook<u32(uptr, Vec3*, f32*, overworld::Facing*, u8*)>
    get_player_movement_hook;
#ifdef GAME_ORAS
// The position of the Circle Pad, from -1 to 1, as the game reads it.
core::Hook<f32(void*)> stick_x_hook;
core::Hook<f32(void*)> stick_y_hook;
#endif

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

// Keeps the turn of the player in its range: kYawLimit on each side of the
// view of the game, or the full circle.
f32 LimitYaw(f32 value) {
  if (kYawLimit >= kPi) {
    if (value > kPi) value -= 2.0f * kPi;
    if (value < -kPi) value += 2.0f * kPi;
    return value;
  }
  return Clamp(value, -kYawLimit, kYawLimit);
}

// Keeps the tilt of the player so that the camera stays between kMinPitch and
// kMaxPitch. A camera of the game that is already out of this range is allowed.
f32 LimitPitchDelta(f32 value, f32 game_pitch) {
  const f32 low = kMinPitch - game_pitch;
  const f32 high = kMaxPitch - game_pitch;
  return Clamp(value, low < 0.0f ? low : 0.0f, high > 0.0f ? high : 0.0f);
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
// to make the matrix of the camera. The hook turns the position around the
// target, by the turn of the player. The target and the distance stay those of
// the game.
Mtx34* UpdateLookAtHook(Mtx34* output, Vec3* pos, Vec3* up, Vec3* target) {
  if (!is_updating_camera) {
    return update_look_at_hook(output, pos, up, target);
  }
  is_updating_camera = false;

  // The camera of the game, as a distance and two angles around its target.
  const f32 off_x = pos->x - target->x;
  const f32 off_y = pos->y - target->y;
  const f32 off_z = pos->z - target->z;
  const f32 flat = std::sqrt(off_x * off_x + off_z * off_z);
  const f32 distance = std::sqrt(flat * flat + off_y * off_y);
  // Not a camera that looks at a point (or not a number): leave it alone.
  if (!(distance > 1.0f)) {
    return update_look_at_hook(output, pos, up, target);
  }
  const f32 game_pitch = std::atan2(off_y, flat);
  const f32 game_yaw = flat > 0.001f ? std::atan2(off_z, off_x) : kDefaultYaw;
  game_pitch_seen = game_pitch;

  // No turn from the player: the camera of the game, as it is.
  if (yaw_delta == 0.0f && pitch_delta == 0.0f && kDistanceScale == 1.0f) {
    return update_look_at_hook(output, pos, up, target);
  }

  const f32 new_pitch =
      Clamp(game_pitch + pitch_delta, game_pitch < kMinPitch ? game_pitch : kMinPitch,
            game_pitch > kMaxPitch ? game_pitch : kMaxPitch);
  const f32 new_yaw = game_yaw + yaw_delta;
  const f32 new_distance = distance * kDistanceScale;
  const f32 new_flat = new_distance * std::cos(new_pitch);
  pos->x = target->x + new_flat * std::cos(new_yaw);
  pos->y = target->y + new_distance * std::sin(new_pitch);
  pos->z = target->z + new_flat * std::sin(new_yaw);
  *up = Vec3(0.0f, 1.0f, 0.0f);

  return update_look_at_hook(output, pos, up, target);
}

// The game reads the controls to move the player. The hook does not change the
// result: it only tells the hooks of the stick that the game is reading the
// controls to walk.
u32 GetPlayerMovementHook(uptr self, Vec3* dir_vec, f32* speed,
                          overworld::Facing* dir, u8* x) {
  is_reading_movement = true;
  const u32 result = get_player_movement_hook(self, dir_vec, speed, dir, x);
  is_reading_movement = false;
  return result;
}

#ifdef GAME_ORAS
// true: the stick that the game reads must be turned by the camera.
bool ShouldTurnStick() {
  if (std::fabs(yaw_delta) < 0.0001f) return false;
  return is_reading_movement || (kTurnStickEverywhere && is_overworld_active);
}

// The game reads the position of the stick with two functions: one for each
// axis. Each hook reads the two real axes, turns them, and gives its own axis.
// The turn keeps the length of the stick: a stick pushed to the end stays
// pushed to the end.
f32 StickXHook(void* stick) {
  const f32 x = stick_x_hook(stick);
  if (!ShouldTurnStick()) return x;
  const f32 y = stick_y_hook(stick);
  f32 turned_x = 0.0f;
  f32 turned_y = 0.0f;
  TurnByCamera(x, y, kDefaultYaw + yaw_delta, &turned_x, &turned_y);
  return turned_x;
}

f32 StickYHook(void* stick) {
  const f32 y = stick_y_hook(stick);
  if (!ShouldTurnStick()) return y;
  const f32 x = stick_x_hook(stick);
  f32 turned_x = 0.0f;
  f32 turned_y = 0.0f;
  TurnByCamera(x, y, kDefaultYaw + yaw_delta, &turned_x, &turned_y);
  return turned_y;
}
#endif

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
#ifdef GAME_ORAS
  stick_x_hook.Install(sys::address::kCirclePadReadX, StickXHook);
  stick_y_hook.Install(sys::address::kCirclePadReadY, StickYHook);
#endif
}

void OnProcessLoad(uptr vtable) {
  // The game loaded the code of a process again: write the hooks again.
  core::HookBase::OnProcessLoad(vtable);
}

void Update() {
  is_overworld_active = core::ProcessManager::IsOverworldActive();
  if (!is_overworld_active) return;

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
  if (is_left && is_right) {
    // L + R together: the view of the game again.
    yaw_delta = 0.0f;
    pitch_delta = 0.0f;
  }
  if (kUseShoulderButtons) {
    if (is_left) yaw_input -= 1.0f;
    if (is_right) yaw_input += 1.0f;
    if (controller.IsKeyDown(Key::kZr)) pitch_input += 1.0f;
    if (controller.IsKeyDown(Key::kZl)) pitch_input -= 1.0f;
  }

  // The view turns to the right when the camera turns clockwise on the map:
  // the yaw grows. The view goes up when the camera goes down: the pitch falls.
  yaw_delta = LimitYaw(yaw_delta + Clamp(yaw_input, -1.0f, 1.0f) * kYawSpeed);
  const f32 pitch_direction = kInvertPitch ? 1.0f : -1.0f;
  pitch_delta += pitch_direction * Clamp(pitch_input, -1.0f, 1.0f) * kPitchSpeed;
  pitch_delta = LimitPitchDelta(pitch_delta, game_pitch_seen);
}

} // namespace freecam
