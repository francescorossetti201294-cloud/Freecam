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
 * @file entrypoint.cc
 * @brief The start of the plugin: Initialize() is called by lib/src/boot.
 *
 * This file replaces plugin.cc and hack.cc of Sango Plugin. It does only
 * two things: it installs the camera, and it runs the camera once for each
 * frame. There is no menu, no screen drawing and no other patch.
 */

#include "common.h"
#include "core/hook.h"
#include "core/native/process_manager.h"
#include "free_camera.h"
#include "system/address.h"

// The static members that the headers of the library declare (they are
// defined in plugin.cc in Sango Plugin).
sys::String sys::String::s_tmp;
c16 sys::String::s_buffer[128];

namespace {

core::Hook<void()> frame_hook;
core::Hook<u32(core::ProcessManager*)> process_loop_hook;

void OnProcessUpdate(uptr /*vtable*/) {}

// The game calls this function at each step of its main loop. When a process
// starts (the overworld, a battle...), the game loads its code again and
// erases the hooks of the code: the camera writes them again.
u32 ProcessLoopHook(core::ProcessManager* manager) {
  manager->Patch(freecam::OnProcessLoad, OnProcessUpdate);
  return process_loop_hook(manager);
}

// The game calls this function once for each frame. It does not call the
// original function (as plugin::Start() of Sango Plugin).
void EveryFrame() { freecam::Update(); }

} // namespace

void Initialize() {
  freecam::Initialize();
  process_loop_hook.Install(sys::address::kMainProcessLoop, ProcessLoopHook);
  frame_hook.Install(sys::address::kEntrypoint, EveryFrame);
}
