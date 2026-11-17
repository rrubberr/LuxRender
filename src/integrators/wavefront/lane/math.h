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

// Lane arithmetic for wavefront integrators.

#ifndef LUX2_INTEGRATORS_LANE_MATH_H
#define LUX2_INTEGRATORS_LANE_MATH_H

#include "core/math/geometry.h"
#include "core/math/vecp.h"

#include <enoki/array.h>

namespace lux2
{
    // a/b, 0 where b <= 0.
    inline FloatP divp(const FloatP &a, const FloatP &b)
    {
        return select(b > FloatP(0.f), a / b, FloatP(0.f));
    }

    // Power heuristic term p^k.
    inline FloatP pw(const FloatP &p, float k)
    {
        if (k == 2.f)
            return p * p;
        if (k == 1.f)
            return p;
        if (k == 0.f)
            return FloatP(1.f);
        return enoki::pow(p, FloatP(k));
    }

    // Power heuristic combining two sampling densities.
    inline FloatP misWeight(const FloatP &aIn, const FloatP &bIn)
    {
        const FloatP a = aIn * aIn;
        const FloatP b = bIn * bIn;
        return select(a > FloatP(0.f), a / (a + b), FloatP(0.f));
    }

    // Solid angle density at p -> area density at pNext (normal ngNext).
    inline FloatP ConvertDensity(const Point3fP &p, FloatP pdf,
                                 const Point3fP &pNext,
                                 const Normal3fP &ngNext)
    {
        const Vector3fP d = pNext - p;
        const FloatP d2 = dot(d, d);
        const FloatP invD2 = divp(FloatP(1.f), d2);
        const Vector3fP w = d * enoki::sqrt(invD2);
        return pdf * abs(dot(ngNext, w)) * invD2;
    }

} // namespace lux2

#endif // LUX2_INTEGRATORS_LANE_MATH_H
