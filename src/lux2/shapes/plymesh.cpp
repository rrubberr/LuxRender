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

#include "shapes/plymesh.h"

#include "core/dynload.h"
#include "core/indexmesh.h"
#include "core/paramset.h"
#include "core/plymesh.h"
#include "core/register.h"

namespace lux2
{

    PlyMeshShape::PlyMeshShape(const Transform &toWorld, std::vector<Point3f> P,
                               std::vector<Normal3f> N, std::vector<UV> uv,
                               std::vector<std::array<int, 3>> tris)
        : m_toWorld(toWorld), m_P(std::move(P)), m_N(std::move(N)),
          m_uv(std::move(uv)), m_tris(std::move(tris))
    {
    }

    BBox PlyMeshShape::WorldBound() const
    {
        return IndexedWorldBound(m_toWorld, m_P);
    }

    void PlyMeshShape::Tessellate(const Transform & /*worldToCamera*/,
                                  std::vector<TriangleDesc> &out) const
    {
        // ReadPlyGeometry always fills N.
        TessellateIndexed(m_toWorld, m_P, m_N, m_uv, m_tris, out, false);
    }

    std::shared_ptr<Shape> PlyMeshShape::CreateShape(const PluginContext &ctx)
    {
        const ParamSet &p = *ctx.params;
        const std::string filename = p.FindOneString("filename", "none");

        std::vector<Point3f> P;
        std::vector<Normal3f> N;
        std::vector<UV> uv;
        std::vector<std::array<int, 3>> tris;
        if (!ReadPlyGeometry(filename, P, N, uv, tris))
        {
            LOG(LUX_ERROR, LUX_BADFILE)
                << "plymesh: failed to read '" << filename << "'";
            return nullptr;
        }
        return std::make_shared<PlyMeshShape>(ctx.transform, std::move(P),
                                              std::move(N), std::move(uv),
                                              std::move(tris));
    }

    LUX2_REGISTER_SHAPE(PlyMeshShape, "plymesh");

} // namespace lux2
