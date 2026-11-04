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

// Shadow transmittance.

#ifndef LUX2_INTEGRATORS_LANE_OCCLUSION_H
#define LUX2_INTEGRATORS_LANE_OCCLUSION_H

#include "integrators/lane/geom.h"

#include "core/bsdf_type.h"
#include "core/bsdfptr_table.h"
#include "core/color/color.h"
#include "core/accel/embree2.h"
#include "core/math/math.h"
#include "core/medium.h"
#include "core/mediumptr_table.h"
#include "core/rng.h"

namespace lux2
{

    // Scene pieces for the occlusion walk.
    struct ShadowCtx
    {
        const EmbreeScene *embree = nullptr;
        const BsdfPtrTable *table = nullptr;
        const MediumPtrTable *mediumTable = nullptr;
        bool hasMedia = false;
        bool hasNull = false;
    };

    // Stream addressing for medium flight inside a shadow ray.
    struct ShadowRng
    {
        UInt32P seed, sampleIdx;
        Int32P bounce, depth;
        UInt32P lightIdx;
        UInt64 salt;
    };

    // Closest hit transmittance from origin along direction over [EPS_RAY, tMax].
    inline SWCSpectrumP Transmittance(const ShadowCtx &c, const Point3fP &origin,
                                      const Vector3fP &dir,
                                      const UInt32P &startMedium,
                                      const FloatP &tMax,
                                      const SpectrumWavelengthsP &sw,
                                      const ShadowRng &rng, MaskP active)
    {
        if (!c.hasMedia && !c.hasNull)
        {
            RayP shadow(origin, dir, FloatP(EPS_RAY), tMax, FloatP(0.f));
            shadow.wavelengths = sw.w;
            shadow.mask = UInt32P(0xFFFFFFFFu);
            const MaskP blocked = c.embree->Occluded(shadow, active, Coherent::No);
            return select(active && !blocked, SWCSpectrumP(1.f),
                          SWCSpectrumP(0.f));
        }

        SWCSpectrumP T(1.f);
        UInt32P curMedium = startMedium;
        FloatP tCur = FloatP(EPS_RAY);
        MaskP tracing = active;
        // Only the medium flight consumes randomness.
        RNGP flight = KeyedRNG(rng.seed, rng.sampleIdx, rng.bounce, rng.depth,
                               rng.lightIdx, rng.salt);

        for (int i = 0; i < MAX_SHADOW_CROSSINGS && any(tracing); ++i)
        {
            RayP shadow(origin, dir, tCur, tMax, FloatP(0.f));
            shadow.wavelengths = sw.w;
            shadow.mask = UInt32P(0xFFFFFFFFu);
            HitP hit;
            // Intersect shades the hit.
            c.embree->Intersect(shadow, hit, Coherent::No);

            const MaskP didHit = tracing && hit.hit;
            const MaskP missed = tracing && !hit.hit;

            // Transmittance over the interval.
            if (c.hasMedia)
            {
                const MediumPtr med = c.mediumTable->Gather(curMedium, tracing);
                const FloatP segEnd = select(didHit, hit.t, tMax);
                T = select(tracing,
                           T * med->Transmittance(sw, shadow, tCur, segEnd, flight,
                                                  tracing),
                           T);
            }

            // Missed everything, reached the light, lane is done.
            tracing = tracing && !missed;
            if (none(tracing))
                break;

            // Null boundaries pass through, opaque occludes.
            const BSDFPtr bsdf = c.table->Gather(hit.matID, tracing);
            const MaskP opaque =
                tracing && !has_flag(bsdf->flags(), BSDFType::Null);
            T = select(opaque, SWCSpectrumP(0.f), T);
            tracing = tracing && !opaque;
            if (any(tracing))
            {
                if (c.hasMedia)
                    curMedium = SwapMedium(hit, dir);
                tCur = select(tracing, hit.t + FloatP(EPS_RAY), tCur);
            }
        }
        // Lanes still tracing after the crossing cap are occluded.
        return select(tracing, SWCSpectrumP(0.f), T);
    }

} // namespace lux2

#endif // LUX2_INTEGRATORS_LANE_OCCLUSION_H
