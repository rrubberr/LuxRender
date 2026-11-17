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

// Scalar helpers for the ERPT chain logic.

#ifndef LUX2_INTEGRATORS_ERPT_ERPT_MATH_H
#define LUX2_INTEGRATORS_ERPT_ERPT_MATH_H

#include "core/rng.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace lux2
{
    namespace erpt
    {
        // numChains = floor(u + e / (m * e_d))

        // E[numChains] * m * e_d == e for any e_d > 0, which makes the
        // redistribution unbiased.
        inline uint32_t NumChains(float e, uint32_t m, float ed, float u,
                                  uint32_t maxChains, bool *clamped)
        {
            *clamped = false;
            if (!(e > 0.f) || !(ed > 0.f) || m == 0u || !std::isfinite(e))
                return 0u;
            const double x = double(u) + double(e) / (double(m) * double(ed));
            if (!(x < double(maxChains)))
            {
                *clamped = true;
                return maxChains;
            }
            return uint32_t(std::floor(x));
        }

        // Metropolis acceptance a = min(1, f(y) T(y->x) / (f(x) T(x->y))).
        // tRatio = T(y->x) / T(x->y).
        inline float AcceptProb(float fx, float fy, float tRatio)
        {
            if (!(fy > 0.f) || !std::isfinite(fy) || !(tRatio > 0.f) ||
                !std::isfinite(tRatio))
                return 0.f;
            if (!(fx > 0.f))
                return 1.f;
            const float a = (fy * tRatio) / fx;
            if (!std::isfinite(a))
                return a > 0.f ? 1.f : 0.f;
            return a >= 1.f ? 1.f : a;
        }

    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_ERPT_MATH_H
