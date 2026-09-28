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

#ifndef LUX2_MATH_H
#define LUX2_MATH_H

// Shared numeric constants.

namespace lux2
{

    constexpr float PI = 3.14159265358979323846f;
    constexpr float INVPI = 0.31830988618379067154f;
    constexpr float INV_TWOPI = 0.15915494309189533577f;
    constexpr float INV_FOURPI = 0.07957747154594766788f;

    inline constexpr float Radians(float deg) { return PI * (1.f / 180.f) * deg; }
    inline constexpr float Degrees(float rad) { return rad * (180.f / PI); }

    // Guard against division by near-zero.
    constexpr float EPS_DENOM = 1e-8f;
    // Ray offsetting / geometric degeneracy thresholds.
    constexpr float EPS_RAY = 1e-4f;

    // Scalar clamp to [low, high].
    inline constexpr float ClampF(float v, float low, float high)
    {
        return v < low ? low : (v > high ? high : v);
    }

} // namespace lux2

#endif // LUX2_MATH_H
