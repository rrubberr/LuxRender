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

// Direct path evaluator for ERPT.

#ifndef LUX2_INTEGRATORS_ERPT_PATH_EVAL_H
#define LUX2_INTEGRATORS_ERPT_PATH_EVAL_H

#include "integrators/core/vertex.h"
#include "integrators/lane/direct.h"
#include "integrators/lane/math.h"
#include "integrators/wf/pool_view.h"
#include "integrators/wf/storage.h"
#include "integrators/wf/types.h"

#include "core/camera.h"
#include "core/film.h"
#include "core/light.h"

#include "integrators/wf/connectors.h"

#include <enoki/array.h>

namespace lux2
{
    namespace erpt
    {
        using wf::PathStore;

        // A vertex's BSDF/phase term toward direction `wi` given the stored
        // vertex data. Returns f*|cos|.
        struct VertexTerm
        {
            SWCSpectrumP f; // BSDF f * cos
            FloatP cosGeo;  // |dot(wi, ng)| at this vertex
            MaskP ok;
        };

        inline VertexTerm EvalVertexTerm(const wf::VertexData &V,
                                         const SpectrumWavelengthsP &sw,
                                         const Vector3fP &wi,
                                         const MaskP &active)
        {
            VertexTerm r;
            r.f = SWCSpectrumP(0.f);
            r.cosGeo = FloatP(0.f);
            r.ok = MaskP(false);

            VertexP vx;
            vx.valid = active;
            vx.isMedium = V.isMed;
            vx.p = V.p;
            vx.dg = V.dg;
            vx.wo = V.wb;
            vx.k = V.k;
            vx.bsdf = V.bsdf;
            vx.med = V.med;

            BSDFEvalP ev = ZeroEval();
            FloatP cosT(0.f);
            EvalVertex(vx, sw, wi, &ev, &cosT, active);
            r.f = ev.f;
            // Geometric normal cosine for the edge term.
            r.cosGeo = select(V.isMed, FloatP(1.f), abs(dot(wi, V.dg.ng)));
            r.ok = active && (ev.f > SWCSpectrumP(0.f));
            return r;
        }

        // Edge term between two positions, |cos at p1 toward p0| / d^2 * Tr.
        struct EdgeTerm
        {
            FloatP g; // |cosFar| / d^2  (or bare 1/d^2 when !farCos)
            SWCSpectrumP Tr;
            MaskP ok;
        };

        inline EdgeTerm EvalEdgeTerm(const ShadowCtx &sc, const Point3fP &p0,
                                     const Normal3fP &ng0, const Vector3fP &dir01,
                                     const Point3fP &p1, const Normal3fP &ng1,
                                     const MaskP &p1IsMed, bool farCos,
                                     const SpectrumWavelengthsP &sw,
                                     const UInt32P &startMed,
                                     const ShadowRng &rng, const MaskP &active)
        {
            EdgeTerm r;
            r.g = FloatP(0.f);
            r.Tr = SWCSpectrumP(0.f);
            r.ok = MaskP(false);

            const Vector3fP dRaw = p1 - p0;
            const FloatP d2 = dot(dRaw, dRaw);
            const MaskP pre = active && (d2 > FloatP(EPS_DENOM));
            if (!any(pre))
                return r;

            // Area measure geometry far cosine over d^2.
            const FloatP cosFar =
                farCos ? select(p1IsMed, FloatP(1.f), abs(dot(dir01, ng1)))
                       : FloatP(1.f);
            r.g = cosFar * divp(FloatP(1.f), d2);

            const FloatP dist = enoki::sqrt(d2);
            const FloatP tmax =
                dist - enoki::max(FloatP(1e-3f), FloatP(1e-3f) * dist);
            const Point3fP so = OffsetRay(p0, ng0, dir01, FloatP(EPS_RAY));
            const Occlusion oc =
                Occlude(sc, so, dir01, startMed, tmax, sw, rng, pre);
            r.Tr = oc.Tr;
            r.ok = oc.ok && (r.g > FloatP(0.f));
            return r;
        }

    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_PATH_EVAL_H
