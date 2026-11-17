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

// Lane tails for wavefront store walkers.

#ifndef LUX2_INTEGRATORS_WF_LANES_H
#define LUX2_INTEGRATORS_WF_LANES_H

#include "integrators/lane/blend.h"
#include "integrators/lane/jobs.h"
#include "integrators/lane/lane_core.h"

#include "core/math/geometry.h"
#include "core/math/ray.h"
#include "core/math/vecp.h"

#include <enoki/array.h>

namespace lux2
{
    namespace wf
    {
        // Eye subpath lane.
        struct EyeLanes
        {
            LaneCore core;
            UInt32P path; // pool path id
            UInt32P slot; // sample slot
            Int32P v;     // vertices written
            FloatP alpha; // film coverage
            FloatP prevPdfR, prevCosi, prevD2;
            Point3fP prevP;
            Normal3fP prevN;
            MaskP prevRecvHasN; // Pdf_L hemisphere test at the previous vertex

            static EyeLanes Fresh(const PixelPlan &plan)
            {
                const RayP &cam = plan.primary;
                EyeLanes s;
                s.core.o = cam.o;
                s.core.d = cam.d;
                s.core.mint = cam.mint;
                s.core.maxt = cam.maxt;
                s.core.time = cam.time;
                s.core.medium = UInt32P(LUX2_MEDIUM_CLEAR);
                s.core.sw = plan.sw;
                s.core.beta = SWCSpectrumP(1.f);
                s.core.seed = plan.seed;
                s.core.sampleIdx = plan.sampleIdx;
                s.core.alive = plan.fill;
                s.path = UInt32P(0u);
                s.slot = plan.sampleIdx;
                s.v = Int32P(0);
                s.alpha = FloatP(0.f);
                s.prevPdfR = FloatP(0.f);
                s.prevCosi = FloatP(0.f);
                s.prevD2 = FloatP(0.f);
                s.prevP = cam.o;
                s.prevN = Normal3fP(0.f);
                s.prevRecvHasN = MaskP(true);
                return s;
            }
        };

        LUX2_BLEND(EyeLanes, core, path, slot, v, alpha, prevPdfR, prevCosi, prevD2,
                   prevP, prevN, prevRecvHasN)

        // Light subpath lane.
        struct LightLanes
        {
            LaneCore core;
            UInt32P path;
            UInt32P sample; // sample slot
            UInt32P lpSlot; // light path index
            UInt32P loBase; // RNG origin dimension base
            Int32P v;       // vertices written
            FloatP prevPdf, prevCoso, prevD2;
            Point3fP prevP;
            Normal3fP prevN;

            static LightLanes Fresh()
            {
                LightLanes s;
                s.core.alive = MaskP(false);
                s.path = UInt32P(0u);
                s.sample = UInt32P(0u);
                s.lpSlot = UInt32P(0u);
                s.loBase = UInt32P(0u);
                s.v = Int32P(0);
                s.prevPdf = FloatP(0.f);
                s.prevCoso = FloatP(0.f);
                s.prevD2 = FloatP(0.f);
                s.prevP = Point3fP(0.f);
                s.prevN = Normal3fP(0.f);
                return s;
            }
        };

        LUX2_BLEND(LightLanes, core, path, sample, lpSlot, loBase, v, prevPdf,
                   prevCoso, prevD2, prevP, prevN)

    } // namespace wf
} // namespace lux2

#endif // LUX2_INTEGRATORS_WF_LANES_H
