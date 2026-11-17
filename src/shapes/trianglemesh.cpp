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

#include "shapes/trianglemesh.h"

#include "core/dynload.h"
#include "core/error.h"
#include "core/indexmesh.h"
#include "core/paramset.h"
#include "core/register.h"

namespace lux2
{

    TriangleMeshShape::TriangleMeshShape(const Transform &toWorld,
                                         std::vector<Point3f> P,
                                         std::vector<Normal3f> N,
                                         std::vector<UV> uv,
                                         std::vector<std::array<int, 3>> tris)
        : m_toWorld(toWorld), m_P(std::move(P)), m_N(std::move(N)),
          m_uv(std::move(uv)), m_tris(std::move(tris))
    {
    }

    BBox TriangleMeshShape::WorldBound() const
    {
        return IndexedWorldBound(m_toWorld, m_P);
    }

    void TriangleMeshShape::Tessellate(const Transform & /*worldToCamera*/,
                                       std::vector<TriangleDesc> &out) const
    {
        // No "N": shade flat. With "N": use.
        TessellateIndexed(m_toWorld, m_P, m_N, m_uv, m_tris, out,
                          /*flatIfNoN=*/true);
    }

    std::shared_ptr<Shape> TriangleMeshShape::CreateShape(
        const PluginContext &ctx)
    {
        const ParamSet &p = *ctx.params;

        std::uint32_t nVerts = 0;
        const Point3f *P = p.FindPoint("P", &nVerts);
        if (!P || nVerts == 0)
        {
            LOG(LUX_ERROR, LUX_SYNTAX)
                << "trianglemesh: no \"P\" vertices given";
            return nullptr;
        }

        // Triangle indices.
        std::uint32_t triCount = 0;
        const int *tris = p.FindInt("indices", &triCount);
        if (!tris)
            tris = p.FindInt("triindices", &triCount);

        // Quad indices.
        std::uint32_t quadCount = 0;
        const int *quads = p.FindInt("quadindices", &quadCount);

        if (!tris && !quads)
        {
            LOG(LUX_ERROR, LUX_SYNTAX)
                << "trianglemesh: neither \"indices\" nor \"quadindices\" given";
            return nullptr;
        }

        // Bound check every index against the vertex count.
        auto inRange = [&](int idx)
        {
            if (idx < 0 || static_cast<std::uint32_t>(idx) >= nVerts)
            {
                LOG(LUX_ERROR, LUX_CONSISTENCY)
                    << "trianglemesh: vertex index " << idx << " out of bounds ("
                    << nVerts << " \"P\" values given)";
                return false;
            }
            return true;
        };

        std::vector<std::array<int, 3>> trisOut;
        if (tris)
        {
            if (triCount % 3 != 0)
            {
                LOG(LUX_ERROR, LUX_CONSISTENCY)
                    << "trianglemesh: \"indices\" count " << triCount
                    << " is not a multiple of 3";
                return nullptr;
            }
            trisOut.reserve(triCount / 3);
            for (std::uint32_t i = 0; i + 2 < triCount; i += 3)
            {
                if (!inRange(tris[i]) || !inRange(tris[i + 1]) ||
                    !inRange(tris[i + 2]))
                    return nullptr;
                trisOut.push_back({tris[i], tris[i + 1], tris[i + 2]});
            }
        }

        // Quads split into (0,1,2) and (0,2,3).
        if (quads)
        {
            if (quadCount % 4 != 0)
            {
                LOG(LUX_ERROR, LUX_CONSISTENCY)
                    << "trianglemesh: \"quadindices\" count " << quadCount
                    << " is not a multiple of 4";
                return nullptr;
            }
            for (std::uint32_t i = 0; i + 3 < quadCount; i += 4)
            {
                if (!inRange(quads[i]) || !inRange(quads[i + 1]) ||
                    !inRange(quads[i + 2]) || !inRange(quads[i + 3]))
                    return nullptr;
                trisOut.push_back({quads[i], quads[i + 1], quads[i + 2]});
                trisOut.push_back({quads[i], quads[i + 2], quads[i + 3]});
            }
        }

        // Optional shading normals, else flat.
        std::vector<Normal3f> N;
        std::uint32_t nNormals = 0;
        const Normal3f *Nraw = p.FindNormal("N", &nNormals);
        if (Nraw)
        {
            if (nNormals == nVerts)
                N.assign(Nraw, Nraw + nVerts);
            else
                LOG(LUX_WARNING, LUX_CONSISTENCY)
                    << "trianglemesh: \"N\" count " << nNormals
                    << " does not match \"P\" count " << nVerts
                    << "; shading flat instead";
        }

        // Optional texture coordinates, else (0,0).
        std::vector<UV> uv;
        std::uint32_t nUV = 0;
        const float *UVraw = p.FindFloat("uv", &nUV);
        if (!UVraw)
            UVraw = p.FindFloat("st", &nUV);
        if (UVraw)
        {
            if (nUV == 2 * nVerts)
            {
                uv.resize(nVerts);
                for (std::uint32_t i = 0; i < nVerts; ++i)
                    uv[i] = UV(UVraw[2 * i], UVraw[2 * i + 1]);
            }
            else
                LOG(LUX_WARNING, LUX_CONSISTENCY)
                    << "trianglemesh: \"uv\" count " << nUV
                    << " does not match 2 * \"P\" count " << (2 * nVerts)
                    << "; using (0,0)";
        }

        return std::make_shared<TriangleMeshShape>(
            ctx.transform, std::vector<Point3f>(P, P + nVerts), std::move(N),
            std::move(uv), std::move(trisOut));
    }

    LUX2_REGISTER_SHAPE(TriangleMeshShape, "trianglemesh");

} // namespace lux2
