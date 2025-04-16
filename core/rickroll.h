/*
    Copyright 2024 Anthony Cruz

    This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include "types.h"

// Rick Roll playback/rendering functions
namespace rickroll {

// Initialize the Rick Roll player
bool init();

// Shutdown the Rick Roll player
void term();

// Returns true if we should render the Rick Roll video frame
bool shouldRender();

// Render the Rick Roll video frame
void render();

// Update Rick Roll state (call every frame)
void update();

} // namespace rickroll 