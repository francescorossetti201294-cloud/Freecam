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
 * @file free_camera.h
 * @brief An orbit camera for the overworld, moved by the right stick.
 *
 * The camera turns around the player. It follows the player when he walks,
 * but it never turns by itself: only the right stick (the C-Stick of the
 * New 3DS, or the Circle Pad Pro) changes its angle. The walking direction is
 * relative to the camera, as in a normal 3D game.
 *
 * Without a right stick, L and R turn the camera, ZL and ZR tilt it.
 * L and R together put the camera behind the player again.
 */

#pragma once

#include "common.h"

namespace freecam {

/// Installs the hooks of the camera and starts the reading of the C-Stick.
void Initialize();

/// Reads the controls. Call it one time for each frame.
void Update();

/// Call it when the game loads a process: it puts the camera behind the
/// player when the overworld starts, and writes the hooks of the process.
void OnProcessLoad(uptr vtable);

} // namespace freecam
