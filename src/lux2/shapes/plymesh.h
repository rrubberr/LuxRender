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

#ifndef LUX2_SHAPE_PLYMESH_H
#define LUX2_SHAPE_PLYMESH_H

#include "core/shape.h"
#include "core/bbox.h"
#include "core/transform.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace lux2 {

class PluginContext;

// PLY triangle mesh.
class PlyMeshShape : public Shape {
public:
    PlyMeshShape(const Transform &toWorld, std::vector<Point3f> P,
                 std::vector<Normal3f> N, std::vector<UV> uv,
                 std::vector<std::array<int, 3>> tris);

    void Tessellate(const Transform &worldToCamera,
                    std::vector<TriangleDesc> &out) const override;
    BBox WorldBound() const override;

    static std::shared_ptr<Shape> CreateShape(const PluginContext &ctx);

private:
    Transform m_toWorld;
    std::vector<Point3f> m_P;
    std::vector<Normal3f> m_N;
    std::vector<UV> m_uv;
    std::vector<std::array<int, 3>> m_tris;
    BBox m_objBound;
};

} // namespace lux2

#endif // LUX2_SHAPE_PLYMESH_H
