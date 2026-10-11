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

#ifndef LUX2_PHASE_H
#define LUX2_PHASE_H

#include "core/vecp.h"
#include "core/geometry.h"
#include "core/math.h"

#include <enoki/array.h>

// Schlick phase function.

namespace lux2
{
    // Schlick k from anisotropy g.
    inline float SchlickK(float g)
    {
        return g * (1.55f - 0.55f * g * g);
    }

    // Clamp |k| below 1 so the pdf stays finite and normalized.
    inline FloatP SchlickKClamp(const FloatP &k)
    {
        constexpr float kMax = 1.f - 1e-6f;
        return enoki::max(FloatP(-kMax), enoki::min(k, FloatP(kMax)));
    }

    // Phase pdf for (wo, wi) where wo points toward the previous
    // vertex; wi is the sampled outgoing direction.
    inline FloatP SchlickPdf(const FloatP &kIn, const Vector3fP &wo,
                             const Vector3fP &wi)
    {
        const FloatP k = SchlickKClamp(kIn);
        const FloatP c = -dot(wo, wi);
        const FloatP den = FloatP(1.f) - k * c;
        return (FloatP(1.f) - k * k) * FloatP(INV_FOURPI) / (den * den);
    }

    // Sample the lobe. wi is built about the propagation direction (-wo),
    // pdf equals SchlickPdf(k, wo, *wi) at the returned direction.
    inline void SchlickSample(const FloatP &kIn, const Vector3fP &wo,
                              const FloatP &u1, const FloatP &u2,
                              Vector3fP *wi, FloatP *pdf)
    {
        const FloatP k = SchlickKClamp(kIn);
        const FloatP u = enoki::min(u1, FloatP(1.f) - FloatP(1e-6f));

        // Invert CDF F(c) = (1-k)(1+c) / (2(1-kc)) for c about the
        // propagation direction.
        const FloatP c = (FloatP(2.f) * u - FloatP(1.f) + k) /
                         (FloatP(1.f) + k * (FloatP(2.f) * u - FloatP(1.f)));

        const FloatP phi = FloatP(2.f * PI) * u2;
        const FloatP sinT =
            enoki::sqrt(enoki::max(FloatP(1.f) - c * c, FloatP(0.f)));

        const Vector3fP fwd = -wo;
        const auto basis = coordinate_system(fwd);
        *wi = c * fwd + (sinT * enoki::cos(phi)) * basis.first +
              (sinT * enoki::sin(phi)) * basis.second;

        *pdf = SchlickPdf(k, wo, *wi);
    }

} // namespace lux2

#endif // LUX2_PHASE_H
