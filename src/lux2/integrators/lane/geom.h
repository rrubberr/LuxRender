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

// Geometric helpers for wavefront integrators.

#ifndef LUX2_INTEGRATORS_LANE_GEOM_H
#define LUX2_INTEGRATORS_LANE_GEOM_H

#include "core/bsdf.h"
#include "core/math/geometry.h"
#include "core/math/ray.h"
#include "core/math/vecp.h"

namespace lux2
{

    // Push a shadow or continuation origin toward the side
    // direction d leaves through.
    inline Point3fP OffsetRay(const Point3fP &p, const Normal3fP &ng,
                              const Vector3fP &d, FloatP k)
    {
        const Normal3fP s = select(dot(d, ng) > FloatP(0.f), ng, -ng);
        return p + Vector3fP(s.x(), s.y(), s.z()) * k;
    }

    // Medium on the side direction leaves a boundary hit.
    inline UInt32P SwapMedium(const HitP &hit, const Vector3fP &dir)
    {
        const MaskP leavesFront = dot(dir, hit.ngeo) > FloatP(0.f);
        return select(leavesFront, hit.mediumFrontID, hit.mediumBackID);
    }

    // Shading frame from a closest hit result.
    inline DifferentialGeometryP MakeDG(const HitP &hit, const Vector3fP &rayD,
                                        const MaskP &m)
    {
        DifferentialGeometryP dg;
        dg.p = hit.p;
        dg.n = select(m, hit.sh_n, Normal3fP(0.f));
        dg.ng = select(m, hit.ngeo, Normal3fP(0.f));
        dg.entering = dot(rayD, hit.ngeo) < FloatP(0.f);
        dg.uv_u = hit.uv.x();
        dg.uv_v = hit.uv.y();
        dg.dp_du = hit.dp_du;
        dg.dp_dv = hit.dp_dv;
        dg.dp_ds = hit.dp_ds;
        dg.dp_dt = hit.dp_dt;
        return dg;
    }

} // namespace lux2

#endif // LUX2_INTEGRATORS_LANE_GEOM_H
