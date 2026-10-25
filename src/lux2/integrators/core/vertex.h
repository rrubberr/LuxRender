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

#ifndef LUX2_INTEGRATORS_VERTEX_H
#define LUX2_INTEGRATORS_VERTEX_H

#include "core/bsdf_call.h"
#include "core/medium_call.h"
#include "core/phase.h"
#include "core/math/ray.h"
#include "core/color/spectrum.h"

#include <enoki/array.h>

namespace lux2
{
    // ---------------------------------------------------------------------------
    // VertexP
    // ---------------------------------------------------------------------------

    // A path vertex is a surface interaction or a medium scatter.
    struct VertexP
    {
        MaskP valid, isMedium;
        Point3fP p;
        DifferentialGeometryP dg; // zeroed on medium lanes
        Vector3fP wo;             // toward the previous vertex
        FloatP k;                 // Schlick k resolved at the event
        BSDFPtr bsdf;             // null on medium lanes
        // For any lane that was inside a medium.
        MediumPtr med;
    };

    // Evaluate the vertex transport term toward wi.
    inline void EvalVertex(const VertexP &v, const SpectrumWavelengthsP &sw,
                           const Vector3fP &wi, BSDFEvalP *ev,
                           FloatP *cosTerm, MaskP active,
                           TransportMode mode = TransportMode::Radiance)
    {
        const MaskP surf = active && !v.isMedium;
        const MaskP med = active && v.isMedium;

        ev->f = SWCSpectrumP(0.f);
        ev->pdf = FloatP(0.f);
        ev->pdfRev = FloatP(0.f);
        *cosTerm = FloatP(0.f);

        if (any(surf))
        {
            // Radiance == reverse (wo toward the eye), no ng Jacobian in
            // Eval.f. ev.f carries |cos(theta_i)|.
            v.bsdf->Eval(sw, wi, v.wo, v.dg, mode, ev, surf);
            enoki::masked(*cosTerm, surf) = abs(dot(wi, v.dg.n));
        }
        if (any(med))
        {
            const FloatP ph = v.med->PhasePdf(sw, v.p, v.wo, wi, med);
            ev->f = select(med, SWCSpectrumP(ph), ev->f);
            ev->pdf = select(med, ph, ev->pdf);
            // Schlick phase is symmetric so the reverse density equals ph.
            ev->pdfRev = select(med, ph, ev->pdfRev);
            enoki::masked(*cosTerm, med) = ph;
        }
    }

    // Sample the continuation direction.
    inline void SampleVertex(const VertexP &v, const SpectrumWavelengthsP &sw,
                             const FloatP &u1, const FloatP &u2,
                             const FloatP &ucomp, BSDFSampleP *s, MaskP active,
                             TransportMode mode = TransportMode::Radiance)
    {
        const MaskP surf = active && !v.isMedium;
        const MaskP med = active && v.isMedium;

        s->wi = Vector3fP(0.f);
        s->f = SWCSpectrumP(0.f);
        s->pdf = FloatP(0.f);
        s->eta = FloatP(1.f);
        s->sampledType = UInt32P(0u);
        s->specular = MaskP(false);

        if (any(surf))
            v.bsdf->SampleF(sw, v.wo, v.dg, u1, u2, ucomp, s, mode, surf);
        if (any(med))
        {
            Vector3fP wiM(FloatP(0.f));
            FloatP pdfTmp(FloatP(0.f));
            SchlickSample(v.k, v.wo, u1, u2, &wiM, &pdfTmp);
            const FloatP q = v.med->PhasePdf(sw, v.p, v.wo, wiM, med);
            s->wi = select(med, wiM, s->wi);
            s->f = select(med, SWCSpectrumP(1.f), s->f);
            s->pdf = select(med, q, s->pdf);
        }
    }

} // namespace lux2

#endif // LUX2_INTEGRATORS_VERTEX_H
