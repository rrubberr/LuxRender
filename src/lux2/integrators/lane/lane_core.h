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

// Lane state for all integrators.

#ifndef LUX2_INTEGRATORS_LANE_LANE_CORE_H
#define LUX2_INTEGRATORS_LANE_LANE_CORE_H

#include "integrators/lane/blend.h"
#include "integrators/lane/jobs.h"

#include "core/color/color.h"
#include "core/math/geometry.h"
#include "core/math/ray.h"
#include "core/math/vecp.h"

#include <enoki/array.h>

namespace lux2
{
    // The segment being traced and the sample identity for random streams.
    struct LaneCore
    {
        Point3fP o;
        Vector3fP d;
        FloatP mint, maxt;
        FloatP time; // for motion blur
        UInt32P medium;
        SpectrumWavelengthsP sw;
        SWCSpectrumP beta; // accumulated throughput
        UInt32P seed, sampleIdx;
        MaskP alive;
    };

    LUX2_BLEND(LaneCore, o, d, mint, maxt, time, medium, sw, beta, seed,
               sampleIdx, alive)

    // Rebuild the Embree boundary ray.
    inline RayP MakeRay(const LaneCore &c)
    {
        RayP r;
        r.o = c.o;
        r.d = c.d;
        r.mint = c.mint;
        r.maxt = c.maxt;
        r.time = c.time;
        r.medium = c.medium;
        r.wavelengths = c.sw.w;
        r.mask = UInt32P(0xFFFFFFFFu);
        r.alive = c.alive;
        return r;
    }

    // Refill dead lanes with blend.
    template <class Lane, class SamplerT>
    void RefillLanes(Lane &lane, const Camera *camera, const SamplerT &sampler,
                     JobCursor &jobs, uint64_t sampleBasis, int k = 2)
    {
        PixelPlan plan;
        if (!PlanPixels(camera, sampler, jobs, sampleBasis, lane.core.alive, k,
                        plan))
            return;
        lane = Blend(plan.fill, Lane::Fresh(plan), lane);
    }

} // namespace lux2

#endif // LUX2_INTEGRATORS_LANE_LANE_CORE_H
