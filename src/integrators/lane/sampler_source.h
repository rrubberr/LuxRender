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

// Bind a sampler to a lane stream (seed, sample index).

#ifndef LUX2_INTEGRATORS_LANE_SAMPLER_SOURCE_H
#define LUX2_INTEGRATORS_LANE_SAMPLER_SOURCE_H

#include "core/math/vecp.h"
#include "core/sampler.h"

namespace lux2
{
    struct SamplerSource
    {
        const Sampler &sampler;
        const UInt32P seed;
        const UInt32P sidx;

        FloatP Get1D(const UInt32P &dim) const
        {
            return sampler.Get1D(seed, sidx, dim);
        }
        Point2fP Get2D(const UInt32P &dim) const
        {
            return sampler.Get2D(seed, sidx, dim);
        }
    };

} // namespace lux2

#endif // LUX2_INTEGRATORS_LANE_SAMPLER_SOURCE_H
