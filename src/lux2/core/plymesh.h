/***************************************************************************
 *   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   LuxRender is free software; you can redistribute it and/or modify     *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   any later version.                                                    *
 *                                                                         *
 *   LuxRender is distributed in the hope that it will be useful,          *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program. If not, see <http://www.gnu.org/licenses/>   *
 *                                                                         *
 *   This project is based on PBRT; see <http://www.pbrt.org>              *
 ***************************************************************************/

#ifndef LUX2_PLYMESH_H
#define LUX2_PLYMESH_H

#include "core/bbox.h"
#include "core/error.h"
#include "core/geometry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lux2 {

// Summary of a .ply file read from its header.
struct PlySummary {
    bool ok = false;
    std::uint64_t faceCount = 0;
    BBox bound; // Object-space bound from vertex positions.
};

// Read a .ply file's face count and object-space bound.
PlySummary ReadPlySummary(const std::string &path);

} // namespace lux2

#endif // LUX2_PLYMESH_H
