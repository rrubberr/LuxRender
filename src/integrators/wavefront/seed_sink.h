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

// Seed capture hook for the wavefront connectors.

#ifndef LUX2_INTEGRATORS_WF_SEED_SINK_H
#define LUX2_INTEGRATORS_WF_SEED_SINK_H

#include "integrators/wavefront/sample_table.h"
#include "integrators/wavefront/storage.h"
#include "integrators/wavefront/types.h"

#include "core/math/vecp.h"

namespace lux2
{
    namespace wf
    {
        enum class SeedKind : int
        {
            Gen = 0,
            Nee = 1,
            CamNee = 2,
            Cam = 3,
            HitLight = 4
        };

        struct SeedRec
        {
            SeedKind kind = SeedKind::Gen;
            MaskP cont;        // lanes that produced a contribution
            FloatP e;          // Lc * w, MIS weighted
            UInt32P sample;    // sample table slot

            const PathStore *eyePs = nullptr;
            UInt32P ePath;
            Int32P nE;         // eye vertices used, indices [0, nE)

            const PathStore *lightPs = nullptr;
            UInt32P lPath;
            Int32P nL;         // light vertices used, indices [0, nL)

            // Virtual emitter point (Nee / CamNee / HitLight).
            Point3fP vP;
            Normal3fP vN, vNg;
            UInt32P vLightIdx; // index into WalkCtx::lightPtrs  (Nee / CamNee)
            UInt32P vAreaId;   // area-light id                  (HitLight)

            // Raster position for strategies that do not live in the sample
            // table row (Cam / CamNee).
            FloatP px, py;

            SeedRec()
                : cont(false), e(0.f), sample(0u), ePath(0u), nE(0), lPath(0u),
                  nL(0), vP(0.f), vN(0.f), vNg(0.f), vLightIdx(0u),
                  vAreaId(0u), px(0.f), py(0.f)
            {
            }
        };

        struct SeedSink
        {
            virtual ~SeedSink() = default;
            virtual void Emit(const WalkCtx &c, const SampleTable &tbl,
                              const SeedRec &rec) = 0;
        };

    } // namespace wf
} // namespace lux2

#endif // LUX2_INTEGRATORS_WF_SEED_SINK_H
