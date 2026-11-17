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

#ifndef LUX2_LIGHT_PHOTOMETRIC_H
#define LUX2_LIGHT_PHOTOMETRIC_H

#include <cmath>
#include <optional>

namespace lux2
{

    // Lux photometric gain factor.
    inline float PhotometricGainFactor(float power, float efficacy,
                                       float geometryTerm,
                                       const std::optional<float> &Y,
                                       bool &outApplied,
                                       float directionalAverage = 1.f)
    {
        outApplied = false;
        if (!Y.has_value())
            return 1.f;

        const double denom = double(geometryTerm) * double(*Y) *
                             double(directionalAverage);
        if (!(denom > 0.0))
            return 1.f;

        const float gainFactor =
            float((double(power) * double(efficacy)) / denom);
        if (gainFactor > 0.f && !std::isinf(gainFactor))
        {
            outApplied = true;
            return gainFactor;
        }
        return 1.f;
    }

} // namespace lux2

#endif // LUX2_LIGHT_PHOTOMETRIC_H
