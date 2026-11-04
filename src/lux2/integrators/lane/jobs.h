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

// Pixel jobs and shared lane refill.

#ifndef LUX2_INTEGRATORS_LANE_JOBS_H
#define LUX2_INTEGRATORS_LANE_JOBS_H

#include "core/camera.h"
#include "core/color/color.h"
#include "core/math/ray.h"
#include "core/math/vecp.h"
#include "core/rng.h"
#include "core/sampler.h"
#include "core/tilequeue.h"

#include <enoki/array.h>

#include <cstdint>

namespace lux2
{
    // One pixel sample to a lane.
    struct Job
    {
        int px, py;
        uint32_t s;
    };

    // Enumerate (px, py, s) jobs over a tile in scanline order.
    struct JobCursor
    {
        int x0, y0, x1, y1;
        uint32_t spp;
        int px, py;
        uint32_t s = 0;

        JobCursor(const Tile &t, uint32_t n)
            : x0(t.x0), y0(t.y0), x1(t.x1), y1(t.y1), spp(n),
              px(t.x0), py(t.y0) {}

        bool Next(Job &j)
        {
            if (s >= spp || x1 <= x0 || y1 <= y0)
                return false;
            j = Job{px, py, s};
            if (++px == x1)
            {
                px = x0;
                if (++py == y1)
                {
                    py = y0;
                    ++s;
                }
            }
            return true;
        }
    };

    // Produce each new lane.
    struct PixelPlan
    {
        MaskP fill;              // lanes taken from the job queue
        UInt32P seed, sampleIdx; // sampler addressing
        FloatP x, y;             // jittered film position
        SpectrumWavelengthsP sw; // sampled wavelengths
        RayP primary;            // camera ray
        FloatP camWeight = FloatP(1.f);
    };

    // Fill dead lanes from the job queue.
    inline bool PlanPixels(const Camera *camera, const Sampler &sampler,
                           JobCursor &jobs, uint64_t sampleBasis,
                           const MaskP &alive, int k, PixelPlan &out,
                           uint32_t maxFill = 0u)
    {
        if (enoki::count(!alive) < k)
            return false;

        alignas(64) float aliveF[PACKET_WIDTH];
        enoki::store_unaligned(aliveF, select(alive, FloatP(1.f), FloatP(0.f)));
        alignas(64) float fillF[PACKET_WIDTH] = {};
        alignas(64) float pxs[PACKET_WIDTH] = {}, pys[PACKET_WIDTH] = {};
        alignas(64) uint32_t seeds[PACKET_WIDTH] = {}, sidxs[PACKET_WIDTH] = {};

        // Fold base seed into a per pixel stream key.
        const uint64_t baseSeed = sampler.BaseSeed();
        bool anyFill = false;
        uint32_t filled = 0u;
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
        {
            if (aliveF[i] > 0.f)
                continue; // lane still live
            if (maxFill != 0u && filled >= maxFill)
                break; // no more slots to admit
            Job j;
            if (!jobs.Next(j))
                break; // jobs exhausted
            ++filled;
            fillF[i] = 1.f;
            pxs[i] = float(j.px);
            pys[i] = float(j.py);
            sidxs[i] = j.s;
            const uint64_t h = PixelSeed(sampleBasis, baseSeed, j.px, j.py);
            seeds[i] = uint32_t(h >> 32);
            anyFill = true;
        }
        if (!anyFill)
            return false;

        // fillF holds 1.0/0.0 floats; derive the mask from a comparison.
        out.fill = enoki::load_unaligned<FloatP>(fillF) > FloatP(0.5f);
        out.seed = enoki::load_unaligned<UInt32P>(seeds);
        out.sampleIdx = enoki::load_unaligned<UInt32P>(sidxs);
        const FloatP pxf = enoki::load_unaligned<FloatP>(pxs);
        const FloatP pyf = enoki::load_unaligned<FloatP>(pys);

        // Primary sample streams.
        const Point2fP jit = sampler.Get2D(out.seed, out.sampleIdx, UInt32P(0u));
        out.sw.Sample(sampler.Get1D(out.seed, out.sampleIdx, UInt32P(1u)));
        out.x = pxf + jit.x();
        out.y = pyf + jit.y();

        if (camera)
        {
            FloatP w;
            camera->GenerateRay(out.x, out.y, FloatP(0.f), &out.primary, &w);
            out.primary.wavelengths = out.sw.w;
            out.primary.mask = UInt32P(0xFFFFFFFFu);
            out.camWeight = w;
        }
        return true;
    }

} // namespace lux2

#endif // LUX2_INTEGRATORS_LANE_JOBS_H
