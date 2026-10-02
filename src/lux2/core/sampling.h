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

#ifndef LUX2_SAMPLING_H
#define LUX2_SAMPLING_H

#include "core/geometry.h"
#include "core/math.h"
#include "core/vecp.h"

namespace lux2
{

    // Formerly LuxRays MC primitives (utils/mc.cpp).

    // Map (u1, u2) in [0,1)^2 to the unit disk.
    inline void concentricSampleDisk(const FloatP &u1, const FloatP &u2,
                                     FloatP *dx, FloatP *dy)
    {
        const FloatP sx = FloatP(2.f) * u1 - FloatP(1.f);
        const FloatP sy = FloatP(2.f) * u2 - FloatP(1.f);

        FloatP r, theta;
        const MaskP r1 = (sx >= -sy) && (sx > sy);   // region 1
        const MaskP r2 = (sx >= -sy) && !(sx > sy);  // region 2
        const MaskP r3 = !(sx >= -sy) && (sx <= sy); // region 3
        r = select(r1, sx, select(r2, sy, select(r3, -sx, -sy)));
        theta = select(r1,
                       select(sy > FloatP(0.f), sy / r,
                              FloatP(8.f) + sy / r),
                       select(r2, FloatP(2.f) - sx / r,
                              select(r3, FloatP(4.f) - sy / r,
                                     FloatP(6.f) + sx / r)));
        theta *= PI * FloatP(0.25f);

        // Degeneracy at the origin (was early return).
        const MaskP origin = (sx == FloatP(0.f)) && (sy == FloatP(0.f));
        enoki::masked(r, origin) = FloatP(0.f);
        enoki::masked(theta, origin) = FloatP(0.f);

        *dx = r * cos(theta);
        *dy = r * sin(theta);
    }

    // Cosine sample the hemisphere around the unit axis n, was
    // Lambertian/OrenNayar SampleF: sample the canonical hemisphere, flip
    // when dot(wo, n) < 0. The caller must reject !sameHemisphere pairs
    // where the sample is in the tangent plane.
    inline Vector3fP cosineSampleHemisphere(const Normal3fP &n,
                                            const Vector3fP &wo,
                                            const FloatP &u1,
                                            const FloatP &u2)
    {
        FloatP px, py;
        concentricSampleDisk(u1, u2, &px, &py);
        const FloatP pz = sqrt(max(FloatP(1.f) - px * px - py * py,
                                   FloatP(0.f)));
        const auto basis = coordinate_system(Vector3fP(n.x(), n.y(), n.z()));
        Vector3fP wi(basis.first * px + basis.second * py +
                     Vector3fP(n.x(), n.y(), n.z()) * pz);
        enoki::masked(wi, dot(wo, n) < FloatP(0.f)) = -wi;
        return wi;
    }

    // Was SameHemisphere (w.z * wp.z > 0) about an arbitrary axis.
    inline MaskP sameHemisphere(const Vector3fP &w, const Vector3fP &wp,
                                const Normal3fP &n)
    {
        return dot(w, n) * dot(wp, n) > FloatP(0.f);
    }

} // namespace lux2

#endif // LUX2_SAMPLING_H
