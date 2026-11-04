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

// Next event estimation.

#ifndef LUX2_INTEGRATORS_LANE_DIRECT_H
#define LUX2_INTEGRATORS_LANE_DIRECT_H

#include "integrators/core/vertex.h"
#include "integrators/lane/geom.h"
#include "integrators/lane/occlusion.h"
#include "integrators/lane/sampler_source.h"
#include "integrators/lane/tile_ctx.h"

#include "core/light.h"
#include "core/light_call.h"
#include "core/math/math.h"
#include "core/renderinghints.h"

#include <enoki/array.h>

namespace lux2
{
    // RNG dimensions for the `one` strategies.
    struct NeeOneDims
    {
        UInt32P sel, pos, comp;
    };

    // Call fn per NEE light sample.
    template <class SrcT, class AllDimsFn, class Fn>
    void ForEachNeeLight(const TileCtx &c, const SrcT &src, const MaskP &gate,
                         const AllDimsFn &allDims, const NeeOneDims &oneDims,
                         Fn &&fn)
    {
        const LightsSamplingStrategy *ls = c.lsD;
        if (!ls || c.nLights <= 0 || !any(gate))
            return;

        if (ls->kind == LightsSamplingStrategy::StrategyKind::All)
        {
            const uint32_t nT = ls->TaskCount();
            for (uint32_t s = 0; s < nT; ++s)
            {
                const uint32_t k = ls->task[s];
                const UInt32P d = allDims(s);
                fn(c.lightPtrs[k], UInt32P(k), UInt32P(s),
                   FloatP(c.lightInf[k]), FloatP(ls->pSel[k]),
                   src.Get2D(d), src.Get1D(d + UInt32P(1u)), gate);
            }
        }
        else
        {
            const LightPick pick = SampleLight(*ls, src.Get1D(oneDims.sel), gate);
            fn(enoki::gather<LightPtr>(c.lightPtrs.data(), pick.idx, gate),
               pick.idx, pick.idx,
               enoki::gather<FloatP>(c.lightInf.data(), pick.idx, gate),
               pick.pSel, src.Get2D(oneDims.pos), src.Get1D(oneDims.comp), gate);
        }
    }

    // Sample the emitter.
    struct LiSample
    {
        MaskP ok;
        Point3fP p;   // sampled point on the light
        Vector3fP wi; // receiver -> light
        Normal3fP n;  // light's normal at p
        FloatP dist, tmax;
        FloatP pdfPos; // area density of p
        FloatP pdfL;   // pdfPos * pSel density
        SWCSpectrumP Le;
    };

    // Sample a point on the light as seen from recvP.
    template <class LightT>
    inline LiSample SampleLi(const SpectrumWavelengthsP &sw, const LightT &light,
                             const FloatP &infF, const Point3fP &recvP,
                             const Normal3fP &recvN, const MaskP &recvHasN,
                             const FloatP &pSel, const Point2fP &uPos,
                             const FloatP &uComp, const MaskP &gate)
    {
        // Enoki arrays are uninitialized on default construction!
        LiSample r;
        r.p = Point3fP(0.f);
        r.wi = Vector3fP(0.f);
        r.n = Normal3fP(0.f);
        r.pdfPos = FloatP(0.f);
        r.Le = SWCSpectrumP(0.f);
        r.ok = light->Sample_Li(sw, recvP, recvN, uPos.x(), uPos.y(), uComp,
                                &r.p, &r.wi, &r.n, &r.pdfPos, &r.Le, gate,
                                recvHasN);
        r.dist = enoki::norm(r.p - recvP);
        r.pdfL = r.pdfPos * pSel;
        // Stop short of a finite emitter to prevent double counting.
        r.tmax = enoki::select(infF > FloatP(0.5f), FloatP(INF_F),
                               r.dist - enoki::max(FloatP(1e-3f),
                                                   FloatP(1e-3f) * r.dist));
        return r;
    }

    // Vertex response and visibility.
    struct Occlusion
    {
        SWCSpectrumP Tr;
        // gate && the ray reached the far end.
        MaskP ok;
    };

    // Trace origin->dir and report the transmittance and survival.
    inline Occlusion Occlude(const ShadowCtx &c, const Point3fP &origin,
                             const Vector3fP &dir, const UInt32P &startMed,
                             const FloatP &tMax, const SpectrumWavelengthsP &sw,
                             const ShadowRng &rng, const MaskP &gate)
    {
        Occlusion r;
        r.Tr = Transmittance(c, origin, dir, startMed, tMax, sw, rng, gate);
        r.ok = gate && (r.Tr > SWCSpectrumP(0.f));
        return r;
    }

    // Shadow origin for a vertex aiming at wi.
    inline Point3fP ShadowOrigin(const VertexP &v, const Vector3fP &wi)
    {
        return OffsetRay(v.p, v.dg.ng, wi, FloatP(EPS_RAY));
    }

    struct DirectEval
    {
        MaskP tryVis; // worth a shadow ray
        MaskP ok;     // tryVis and the ray reached the light
        BSDFEvalP ev;
        // |dot(wi, n)| on surface lanes, phase pdf on medium lanes.
        FloatP cosTerm;
        SWCSpectrumP Tr;
    };

    // Evaluate the vertex toward the light, then trace if it can contribute.
    template <class StartMedFn>
    inline DirectEval ShadeDirect(const ShadowCtx &sc, const VertexP &vtx,
                                  const SpectrumWavelengthsP &sw,
                                  const LiSample &li, const ShadowRng &rng,
                                  const StartMedFn &startMed)
    {
        DirectEval r;
        r.ev = ZeroEval();
        r.cosTerm = FloatP(0.f);
        r.Tr = SWCSpectrumP(0.f);

        EvalVertex(vtx, sw, li.wi, &r.ev, &r.cosTerm, li.ok);
        r.tryVis = li.ok && (li.pdfL > FloatP(0.f)) &&
                   (r.ev.f > SWCSpectrumP(0.f));
        if (none(r.tryVis))
            return r;

        const Occlusion oc =
            Occlude(sc, ShadowOrigin(vtx, li.wi), li.wi, startMed(li.wi),
                    li.tmax, sw, rng, r.tryVis);
        r.Tr = oc.Tr;
        r.ok = oc.ok;
        return r;
    }

} // namespace lux2

#endif // LUX2_INTEGRATORS_LANE_DIRECT_H
