/***************************************************************************
 * Copyright 1998-2026 by authors (see AUTHORS.txt)                        *
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

#ifndef LUX2_SHAPE_H
#define LUX2_SHAPE_H

#include "core/geometry.h"
#include "core/bbox.h"
#include "core/transform.h"

#include <vector>
#include <cstdint>

namespace lux2
{

    // One world space triangle produced by tessellation.
    struct TriangleDesc
    {
        Point3f v0, v1, v2;                    // world space vertices
        Normal3f n0, n1, n2;                   // per-vertex shading normals
        UV uv0, uv1, uv2;                      // per-vertex texture coords
        std::uint32_t matID;                   // resolved material index
        std::int32_t lightID;                  // area light index, or -1
        std::uint32_t groupMask = 0xFFFFFFFFu; // visibility group mask
    };

    // Abstract shape.
    class Shape
    {
    public:
        virtual ~Shape() = default;

        // Append world space triangles to output.
        virtual void Tessellate(const Transform &worldToCamera,
                                std::vector<TriangleDesc> &out) const = 0;

        // Axis aligned world space bound of the shape.
        virtual BBox WorldBound() const = 0;
    };

} // namespace lux2

#endif // LUX2_SHAPE_H
