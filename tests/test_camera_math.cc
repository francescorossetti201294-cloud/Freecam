// Host test of the geometry of the free camera (not part of the plugin).
//   g++ -std=gnu++11 -fpermissive -w -I<stub 3ds.h> -Ilib/include -Ilib/src -Isrc \
//       tests/test_camera_math.cc -o /tmp/test_camera_math && /tmp/test_camera_math
// It includes the .cc to reach the functions of its anonymous namespace.
#include "../src/free_camera.cc"

#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); failures++; } } while (0)

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

static void ExpectTurn(const char* name, float yaw, float e, float n, float we, float wn) {
  float oe, on;
  freecam::TurnByCamera(e, n, yaw, &oe, &on);
  if (!Near(oe, we) || !Near(on, wn)) {
    std::printf("FAIL: %s -> got (%.3f, %.3f) expected (%.3f, %.3f)\n", name, oe, on, we, wn);
    failures++;
  }
}

int main() {
  const float pi = 3.14159265f;
  // Default camera (south of the player, looks north): nothing changes.
  ExpectTurn("default up",    pi / 2,  0,  1,  0,  1);
  ExpectTurn("default right", pi / 2,  1,  0,  1,  0);
  ExpectTurn("default down",  pi / 2,  0, -1,  0, -1);
  // Camera west of the player (looks east): up = east, right = south.
  ExpectTurn("west cam up",    pi,  0,  1,  1,  0);
  ExpectTurn("west cam right", pi,  1,  0,  0, -1);
  ExpectTurn("west cam left",  pi, -1,  0,  0,  1);
  ExpectTurn("west cam down",  pi,  0, -1, -1,  0);
  // Camera east of the player (looks west): up = west, right = north.
  ExpectTurn("east cam up",    0,  0,  1, -1,  0);
  ExpectTurn("east cam right", 0,  1,  0,  0,  1);
  // Camera north of the player (looks south): everything is reversed.
  ExpectTurn("north cam up",    -pi / 2,  0,  1,  0, -1);
  ExpectTurn("north cam right", -pi / 2,  1,  0, -1,  0);

  // The 8 directions.
  using overworld::Facing;
  CHECK(freecam::QuantizeDirection( 1,  0) == Facing::kRight,     "q east");
  CHECK(freecam::QuantizeDirection( 1,  1) == Facing::kUpRight,   "q ne");
  CHECK(freecam::QuantizeDirection( 0,  1) == Facing::kUp,        "q north");
  CHECK(freecam::QuantizeDirection(-1,  1) == Facing::kUpLeft,    "q nw");
  CHECK(freecam::QuantizeDirection(-1,  0) == Facing::kLeft,      "q west");
  CHECK(freecam::QuantizeDirection(-1, -1) == Facing::kDownLeft,  "q sw");
  CHECK(freecam::QuantizeDirection( 0, -1) == Facing::kDown,      "q south");
  CHECK(freecam::QuantizeDirection( 1, -1) == Facing::kDownRight, "q se");
  CHECK(freecam::QuantizeDirection( 0.38f, 0.92f) == Facing::kUp,      "q 67 deg -> north");
  CHECK(freecam::QuantizeDirection( 0.92f, 0.38f) == Facing::kRight,   "q 22 deg -> east");

  // The camera position for the default yaw is south (+z) of the player.
  {
    float yaw = pi / 2, pitch = 0.55f, d = freecam::kDistance;
    float flat = d * std::cos(pitch);
    float px = flat * std::cos(yaw), pz = flat * std::sin(yaw);
    CHECK(Near(px, 0) && pz > 0, "default camera is south of the player");
  }
  // The yaw that puts the camera behind the player (the code of
  // ResetBehindPlayer): the player looks north (z < 0): the camera is south.
  {
    auto behind = [](float fx, float fz) { return std::atan2(-fz, -fx); };
    float y = behind(0, -1);
    CHECK(Near(std::cos(y), 0) && std::sin(y) > 0.99f, "behind a player who looks north");
    y = behind(1, 0);
    CHECK(Near(std::cos(y), -1), "behind a player who looks east: camera in the west");
  }
  // Yaw grows -> the view turns to the right (clockwise on the map).
  {
    float y0 = pi / 2, y1 = pi / 2 + 0.1f;
    float v0e = -std::cos(y0), v0n = std::sin(y0);
    float v1e = -std::cos(y1), v1n = std::sin(y1);
    float cross = v0e * v1n - v0n * v1e; // < 0: clockwise
    CHECK(cross < 0, "yaw+ turns the view clockwise (to the right)");
  }

  // The dead zone and the shape of the stick.
  CHECK(freecam::ShapeStick(0.1f) == 0.0f, "dead zone");
  CHECK(Near(freecam::ShapeStick(1.0f), 1.0f), "full tilt");
  CHECK(Near(freecam::ShapeStick(-1.0f), -1.0f), "full tilt negative");

  if (failures == 0) std::printf("All camera math tests passed.\n");
  return failures ? 1 : 0;
}
