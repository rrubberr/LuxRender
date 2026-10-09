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

#ifndef LUX2_INDEXMESH_H
#define LUX2_INDEXMESH_H

#include "core/bbox.h"
#include "core/geometry.h"
#include "core/shape.h"
#include "core/transform.h"

#include <array>
#include <vector>

namespace lux2
{

    // Transform an indexed triangle soup (object space) into world space
    // TriangleDesc entries.
    inline void TessellateIndexed(const Transform &toWorld,
                                  const std::vector<Point3f> &P,
                                  const std::vector<Normal3f> &N,
                                  const std::vector<UV> &uv,
                                  const std::vector<std::array<int, 3>> &tris,
                                  std::vector<TriangleDesc> &out,
                                  bool flatIfNoN)
    {
        const bool haveN = !N.empty();
        for (const auto &t : tris)
        {
            const int i0 = t[0], i1 = t[1], i2 = t[2];
            TriangleDesc td;
            td.v0 = toWorld * P[i0];
            td.v1 = toWorld * P[i1];
            td.v2 = toWorld * P[i2];
            if (haveN)
            {
                td.n0 = toWorld * N[i0];
                td.n1 = toWorld * N[i1];
                td.n2 = toWorld * N[i2];
            }
            else if (flatIfNoN)
            {
                // Matches Embree's geometric normal Ng = cross(v1-v0, v2-v0).
                const Normal3f fn =
                    enoki::cross(td.v1 - td.v0, td.v2 - td.v0);
                td.n0 = td.n1 = td.n2 = fn;
            }
            if (!uv.empty())
            {
                td.uv0 = uv[i0];
                td.uv1 = uv[i1];
                td.uv2 = uv[i2];
            }
            out.push_back(td);
        }
    }

    // Transform the eight corners of the object space bound of P.
    inline BBox IndexedWorldBound(const Transform &toWorld,
                                  const std::vector<Point3f> &P)
    {
        BBox obj;
        for (const Point3f &p : P)
            obj = Union(obj, p);
        if (!obj.IsValid())
            return BBox();
        BBox wb;
        for (int i = 0; i < 8; ++i)
        {
            const Point3f c((i & 1) ? obj.pMax.x() : obj.pMin.x(),
                            (i & 2) ? obj.pMax.y() : obj.pMin.y(),
                            (i & 4) ? obj.pMax.z() : obj.pMin.z());
            wb = Union(wb, toWorld * c);
        }
        return wb;
    }

} // namespace lux2

#endif // LUX2_INDEXMESH_H
